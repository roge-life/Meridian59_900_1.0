variable "do_token" {
  description = "DigitalOcean API Token"
  type        = string
  sensitive   = true
}

variable "region" {
  description = "DigitalOcean Region"
  type        = string
  default     = "nyc3"
}

variable "droplet_size" {
  description = "Droplet Size"
  type        = string
  default     = "s-1vcpu-2gb"
}

variable "ssh_key_name" {
  description = "The name of the SSH key on DigitalOcean to grant root access"
  type        = string
}

variable "domain_name" {
  description = "Base domain name"
  type        = string
  default     = "emfiftynine.info"
}

variable "subdomain" {
  description = "Subdomain for the server"
  type        = string
  default     = "900"
}
