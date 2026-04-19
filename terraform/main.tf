terraform {
  required_providers {
    digitalocean = {
      source  = "digitalocean/digitalocean"
      version = "~> 2.0"
    }
  }
}

provider "digitalocean" {
  token = var.do_token
}

data "digitalocean_ssh_key" "main" {
  name = var.ssh_key_name
}

resource "digitalocean_droplet" "m59_server" {
  image     = "ubuntu-24-04-x64"
  name      = "meridian-900-server"
  region    = var.region
  size      = var.droplet_size
  ssh_keys  = [data.digitalocean_ssh_key.main.id]
  user_data = file("${path.module}/userdata.sh")
}

resource "digitalocean_firewall" "m59_firewall" {
  name = "meridian-59-firewall"

  droplet_ids = [digitalocean_droplet.m59_server.id]

  inbound_rule {
    protocol         = "tcp"
    port_range       = "22"
    source_addresses = ["0.0.0.0/0", "::/0"]
  }

  inbound_rule {
    protocol         = "tcp"
    port_range       = "5959"
    source_addresses = ["0.0.0.0/0", "::/0"]
  }

  inbound_rule {
    protocol         = "udp"
    port_range       = "5959"
    source_addresses = ["0.0.0.0/0", "::/0"]
  }

  outbound_rule {
    protocol              = "tcp"
    port_range            = "1-65535"
    destination_addresses = ["0.0.0.0/0", "::/0"]
  }

  outbound_rule {
    protocol              = "udp"
    port_range            = "1-65535"
    destination_addresses = ["0.0.0.0/0", "::/0"]
  }

  outbound_rule {
    protocol              = "icmp"
    destination_addresses = ["0.0.0.0/0", "::/0"]
  }
}

resource "digitalocean_record" "server_record" {
  domain = var.domain_name
  type   = "A"
  name   = var.subdomain
  value  = digitalocean_droplet.m59_server.ipv4_address
}
