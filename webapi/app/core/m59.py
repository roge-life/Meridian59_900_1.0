import httpx
import logging

logger = logging.getLogger(__name__)

class M59Client:
    def __init__(self, host: str, port: int = 9999):
        # We now connect to the M59 Bridge on port 9999
        self.base_url = f"http://{host}:{port}"

    def send_command(self, command: str):
        """
        Sends a command to the Meridian 59 server via the HTTP Bridge.
        """
        try:
            with httpx.Client(timeout=10.0) as client:
                response = client.post(
                    f"{self.base_url}/execute",
                    json={"command": command}
                )
                response.raise_for_status()
                data = response.json()
                output = data.get("output", "").strip()
                
                # Strip echoed command if present
                if output.startswith(">"):
                    lines = output.split('\n')
                    if len(lines) > 1:
                        output = "\n".join(lines[1:]).strip()
                
                logger.info(f"M59 Bridge Response: {output[:100]}...")
                return output
        except Exception as e:
            logger.error(f"M59 Bridge Error: {e}")
            raise Exception(f"Failed to communicate with M59 Bridge: {e}")

    def create_account(self, name, password, email, account_type="user"):
        if account_type in ("admin", "dm"):
            cmd = f"create account {account_type} {name} {password} {email}"
        else:
            cmd = f"create automated {name} {password} {email}"
        return self.send_command(cmd)

    def get_user_info(self, name):
        # show account <name>
        cmd = f"show account {name}"
        return self.send_command(cmd)

    def set_password(self, name, new_password):
        # 1. Resolve name to ID
        info = self.get_user_info(name)
        import re
        match = re.search(r"Account (\d+):", info)
        if not match:
            # Fallback for table format
            match = re.search(r"^\s*(\d+)\s+" + re.escape(name), info, re.MULTILINE)

        if not match:
            raise Exception(f"Could not find account ID for {name}")

        account_id = match.group(1)
        cmd = f"set account password {account_id} {new_password}"
        return self.send_command(cmd)

    # ── Server events ──────────────────────────────────────────────────────

    def get_chaos_night(self):
        out = self.send_command("send object 0 GetChaosNight")
        # Active when return value is a non-zero object ID, not INT 0
        return {"active": "INT 0" not in out, "raw": out}

    def start_chaos_night(self, duration=60):
        return self.send_command(f"send object 0 StartChaosNight #duration {duration}")

    def end_chaos_night(self):
        return self.send_command("send object 0 EndChaosNight")

    def update_hall_of_heroes(self):
        return self.send_command("send object 0 UpdateHallOfHeroes")

    def recreate_room_rental(self):
        return self.send_command("send object 0 RecreateRentableRoomMaintenance")
