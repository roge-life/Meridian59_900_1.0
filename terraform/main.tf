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
  spaces_access_id  = var.spaces_access_id
  spaces_secret_key = var.spaces_secret_key
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

resource "digitalocean_droplet" "m59_web_api" {
  image     = "ubuntu-24-04-x64"
  name      = "meridian-900-web-api"
  region    = var.region
  size      = var.droplet_size
  ssh_keys  = [data.digitalocean_ssh_key.main.id]
  user_data = file("${path.module}/userdata_web.sh")
}

resource "random_id" "bucket_suffix" {
  byte_length = 4
}

resource "digitalocean_spaces_bucket" "m59_backups" {
  name   = "m59-900-backups-${random_id.bucket_suffix.hex}"
  region = var.region
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

  # Allow Maintenance Port and Bridge from the Web API droplet
  inbound_rule {
    protocol         = "tcp"
    port_range       = "9998-9999"
    source_droplet_ids = [digitalocean_droplet.m59_web_api.id]
  }

  inbound_rule {
    protocol         = "icmp"
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

resource "digitalocean_firewall" "m59_web_firewall" {
  name = "meridian-900-web-firewall"

  droplet_ids = [digitalocean_droplet.m59_web_api.id]

  inbound_rule {
    protocol         = "tcp"
    port_range       = "22"
    source_addresses = ["0.0.0.0/0", "::/0"]
  }

  inbound_rule {
    protocol         = "tcp"
    port_range       = "80"
    source_addresses = ["0.0.0.0/0", "::/0"]
  }

  inbound_rule {
    protocol         = "tcp"
    port_range       = "443"
    source_addresses = ["0.0.0.0/0", "::/0"]
  }

  # Allow MySQL access from the game server
  inbound_rule {
    protocol         = "tcp"
    port_range       = "3306"
    source_droplet_ids = [digitalocean_droplet.m59_server.id]
  }

  inbound_rule {
    protocol         = "icmp"
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
