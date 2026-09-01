#!/bin/bash
# Apply the dev Terraform stack.
# Reads DO token from ~/do_token.txt; uses `gh auth token` for GitHub.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TF_DIR="$REPO_ROOT/terraform-dev"

# --- RETIRED 2026-09-01 -------------------------------------------------
# The m59 dev environment was destroyed (owner-confirmed) and terraform-dev's
# state has been cleaned of the four dead DigitalOcean resources. main.tf still
# DECLARES them, so running this script would happily recreate two droplets and
# two firewalls -- roughly $24/mo of billing for an environment nobody uses.
# That is the "state drift" flagged in the diogenes repo (cloud.md, billing.md).
#
# Disabled deliberately, with an escape hatch rather than deletion:
#     M59_DEV_REALLY_APPLY=1 scripts/apply_dev_terraform.sh
if [ "${M59_DEV_REALLY_APPLY:-0}" != "1" ]; then
    echo "REFUSING: the m59 dev environment is retired (2026-09-01)." >&2
    echo "  This would recreate 2 droplets + 2 firewalls, ~\$24/mo." >&2
    echo "  Set M59_DEV_REALLY_APPLY=1 if that is genuinely what you want." >&2
    exit 1
fi
# ------------------------------------------------------------------------

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
