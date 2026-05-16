#!/usr/bin/env python3
"""
M59 HTTP bridge: translates POST /execute {"command":"..."} into the
raw TCP maintenance protocol on port 9998 and returns {"output":"..."}.
Runs on port 9999, protected by the DO firewall (web API droplet only).
Pure stdlib — no pip install required.
"""
import json
import os
import socket
from http.server import BaseHTTPRequestHandler, HTTPServer
from threading import Thread

MAINTENANCE_HOST = os.getenv("M59_HOST", "127.0.0.1")
MAINTENANCE_PORT = int(os.getenv("M59_MAINTENANCE_PORT", 9998))
BRIDGE_PORT      = int(os.getenv("BRIDGE_PORT", 9999))


def send_tcp_command(command: str) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        s.connect((MAINTENANCE_HOST, MAINTENANCE_PORT))
        # drain greeting
        s.settimeout(0.5)
        try:
            s.recv(4096)
        except socket.timeout:
            pass
        s.settimeout(5)
        s.sendall(f"{command}\r\n".encode())
        response = s.recv(8192)
        try:
            s.sendall(b"quit\r\n")
        except Exception:
            pass
    return response.decode(errors="replace").strip()


class BridgeHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/execute":
            self.send_response(404)
            self.end_headers()
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length))
            output = send_tcp_command(body["command"])
            self._json(200, {"output": output})
        except Exception as e:
            self._json(500, {"error": str(e)})

    def _json(self, code, data):
        payload = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", len(payload))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    server = HTTPServer(("0.0.0.0", BRIDGE_PORT), BridgeHandler)
    print(f"M59 bridge listening on :{BRIDGE_PORT} -> {MAINTENANCE_HOST}:{MAINTENANCE_PORT}")
    server.serve_forever()
