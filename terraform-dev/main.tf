terraform {
  required_providers {
    digitalocean = {
      source  = "digitalocean/digitalocean"
      version = "~> 2.0"
    }
    random = {
      source  = "hashicorp/random"
      version = "~> 3.0"
    }
  }
}

resource "random_password" "db_password" {
  length  = 24
  special = false
}

resource "random_password" "secret_key" {
  length  = 48
  special = false
}

provider "digitalocean" {
  token = var.do_token
}

data "digitalocean_ssh_key" "main" {
  name = var.ssh_key_name
}

resource "digitalocean_droplet" "m59_dev_server" {
  image    = "ubuntu-24-04-x64"
  name     = "meridian-900-dev-server"
  region   = var.region
  size     = var.droplet_size
  ssh_keys = [data.digitalocean_ssh_key.main.id]
  user_data = templatefile("${path.module}/userdata_gameserver.sh", {
    github_token = var.github_token
    github_org   = var.github_org
    github_repo  = var.github_repo
  })
}

resource "digitalocean_droplet" "m59_dev_web_api" {
  image    = "ubuntu-24-04-x64"
  name     = "meridian-900-dev-web-api"
  region   = var.region
  size     = var.droplet_size
  ssh_keys = [data.digitalocean_ssh_key.main.id]
  user_data = templatefile("${path.module}/userdata_web.sh", {
    github_token   = var.github_token
    github_org     = var.github_org
    github_repo    = var.github_repo
    db_password    = random_password.db_password.result
    game_server_ip = digitalocean_droplet.m59_dev_server.ipv4_address
    domain         = var.domain
    secret_key     = random_password.secret_key.result
  })
}

resource "digitalocean_firewall" "m59_dev_gameserver" {
  name        = "meridian-900-dev-gameserver-firewall"
  droplet_ids = [digitalocean_droplet.m59_dev_server.id]

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
    protocol           = "tcp"
    port_range         = "9998-9999"
    source_droplet_ids = [digitalocean_droplet.m59_dev_web_api.id]
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

resource "digitalocean_firewall" "m59_dev_web" {
  name        = "meridian-900-dev-web-firewall"
  droplet_ids = [digitalocean_droplet.m59_dev_web_api.id]

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

  inbound_rule {
    protocol           = "tcp"
    port_range         = "3306"
    source_droplet_ids = [digitalocean_droplet.m59_dev_server.id]
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
