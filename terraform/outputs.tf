output "server_ip" {
  description = "Public IP address of the Meridian 59 server"
  value       = digitalocean_droplet.m59_server.ipv4_address
}
