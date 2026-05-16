output "dev_server_ip" {
  description = "Public IP of the dev game server"
  value       = digitalocean_droplet.m59_dev_server.ipv4_address
}

output "dev_web_api_ip" {
  description = "Public IP of the dev web API"
  value       = digitalocean_droplet.m59_dev_web_api.ipv4_address
}

output "ssh_game_server" {
  description = "SSH command for the dev game server"
  value       = "ssh root@${digitalocean_droplet.m59_dev_server.ipv4_address}"
}

output "ssh_web_api" {
  description = "SSH command for the dev web API"
  value       = "ssh root@${digitalocean_droplet.m59_dev_web_api.ipv4_address}"
}

output "db_password" {
  description = "Generated MariaDB password for m59api"
  value       = random_password.db_password.result
  sensitive   = true
}

output "secret_key" {
  description = "Generated FastAPI session signing key"
  value       = random_password.secret_key.result
  sensitive   = true
}

output "next_steps" {
  description = "Post-provision checklist"
  value       = <<-EOT
    DNS records to create:
      ${var.game_server_domain}  A  ${digitalocean_droplet.m59_dev_server.ipv4_address}
      ${var.domain}              A  ${digitalocean_droplet.m59_dev_web_api.ipv4_address}

    1. Watch bootstrap logs (game server):
         ssh root@${digitalocean_droplet.m59_dev_server.ipv4_address} journalctl -u cloud-final -f
    2. Check blakserv is running:
         ssh root@${digitalocean_droplet.m59_dev_server.ipv4_address} systemctl status blakserv
    3. Once DNS propagates for ${var.domain}, enable HTTPS:
         ssh root@${digitalocean_droplet.m59_dev_web_api.ipv4_address}
         certbot --nginx -d ${var.domain}
  EOT
}
