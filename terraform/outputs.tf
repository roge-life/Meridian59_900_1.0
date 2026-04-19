output "server_ip" {
  description = "Public IP address of the Meridian 59 server"
  value       = digitalocean_droplet.m59_server.ipv4_address
}

output "server_fqdn" {
  description = "Fully qualified domain name of the server"
  value       = "${var.subdomain}.${var.domain_name}"
}
