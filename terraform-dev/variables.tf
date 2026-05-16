variable "do_token" {
  description = "DigitalOcean API Token"
  type        = string
  sensitive   = true
}

variable "region" {
  description = "DigitalOcean region slug"
  type        = string
  default     = "nyc3"
}

variable "droplet_size" {
  description = "Droplet size slug"
  type        = string
  default     = "s-1vcpu-2gb"
}

variable "ssh_key_name" {
  description = "Name of the SSH key registered in DigitalOcean"
  type        = string
  default     = "ed-key-bazzite"
}

variable "github_token" {
  description = "GitHub PAT with actions:read and repo (read) scopes"
  type        = string
  sensitive   = true
}

variable "github_org" {
  description = "GitHub organization or user owning the repo"
  type        = string
  default     = "roge-life"
}

variable "github_repo" {
  description = "GitHub repository name"
  type        = string
  default     = "Meridian59_900_1.0"
}

variable "domain" {
  description = "Fully-qualified domain for the web API"
  type        = string
  default     = "dev.emfiftynine.info"
}

variable "game_server_domain" {
  description = "Fully-qualified domain clients use to reach the game server (port 5959)"
  type        = string
  default     = "901.emfiftynine.info"
}

