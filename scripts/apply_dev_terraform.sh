#!/bin/bash
# Apply the dev Terraform stack.
# Reads DO token from ~/do_token.txt; uses `gh auth token` for GitHub.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TF_DIR="$REPO_ROOT/terraform-dev"

DO_TOKEN_FILE="$HOME/do_token.txt"
if [ ! -f "$DO_TOKEN_FILE" ]; then
    echo "ERROR: $DO_TOKEN_FILE not found" >&2
    exit 1
fi

export TF_VAR_do_token
TF_VAR_do_token=$(tr -d '[:space:]' < "$DO_TOKEN_FILE")

export TF_VAR_github_token
TF_VAR_github_token=$(gh auth token)

# --- Apply ---
cd "$TF_DIR"
terraform init -upgrade -input=false
terraform apply "$@"
