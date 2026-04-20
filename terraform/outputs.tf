output "server_ip" {
  description = "Public IP address of the Meridian 59 server"
  value       = digitalocean_droplet.m59_server.ipv4_address
}

output "web_api_ip" {
  description = "Public IP address of the Meridian 59 Web API"
  value       = digitalocean_droplet.m59_web_api.ipv4_address
}
