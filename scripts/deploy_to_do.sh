#!/bin/bash
# Deployment script for Meridian 59 Server 900 to DigitalOcean

# 0. Configuration
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
TERRAFORM_DIR="$REPO_ROOT/terraform"
LOCAL_RUN_DIR="$REPO_ROOT/run/server"
REMOTE_DIR="/opt/meridian59"
REMOTE_USER="root"

echo "🔍 Fetching server IP from Terraform..."
SERVER_IP=$(terraform -chdir="$TERRAFORM_DIR" output -raw server_ip 2>/dev/null)

if [ -z "$SERVER_IP" ] || [[ "$SERVER_IP" == *"No outputs"* ]]; then
    echo "❌ Error: Could not retrieve server IP from Terraform."
    exit 1
fi

echo "📍 Target IP: $SERVER_IP"

# 1. Local Cleanup & Permission Fix
echo "🧹 Cleaning up local run directory (fixing makefile artifacts)..."
# Remove directories with literal backslashes created by broken makefiles
find "$LOCAL_RUN_DIR" -maxdepth 1 -name "*\\*" -exec rm -rf {} +

# Ensure local binary is executable
if [ -f "$LOCAL_RUN_DIR/blakserv" ]; then
    chmod +x "$LOCAL_RUN_DIR/blakserv"
else
    echo "⚠️ Warning: $LOCAL_RUN_DIR/blakserv not found! Deployment may be incomplete."
fi

# 2. Sync files to the remote server
echo "🚀 Starting rsync to $SERVER_IP..."
# Exclude logs, input files, and live state to avoid overwriting remote data
rsync -avz --progress \
    -e "ssh -o StrictHostKeyChecking=no" \
    --exclude 'channel/*' \
    --exclude 'input.txt' \
    --exclude 'server_run.log' \
    --exclude 'create_m59_account.py' \
    --exclude '*.log' \
    --exclude 'savegame/*' \
    "$LOCAL_RUN_DIR/" "$REMOTE_USER@$SERVER_IP:$REMOTE_DIR/"

# 3. Remote Permission Fix & Service Restart
echo "🔧 Finalizing remote setup and permissions..."
ssh -o StrictHostKeyChecking=no "$REMOTE_USER@$SERVER_IP" <<EOF
    echo "👤 Fixing ownership to meridian:meridian..."
    chown -R meridian:meridian $REMOTE_DIR
    
    echo "🔑 Ensuring binary and directories have correct permissions..."
    chmod +x $REMOTE_DIR/blakserv
    # Ensure meridian user can write to critical game directories
    chmod 775 $REMOTE_DIR/savegame $REMOTE_DIR/channel $REMOTE_DIR/rsc $REMOTE_DIR/loadkod
    
    echo "🔄 Restarting blakserv service..."
    systemctl restart blakserv
    
    # Brief status check
    sleep 2
    systemctl status blakserv --no-pager | head -n 15
    echo "✅ Remote setup complete."
EOF

echo "✨ Deployment successful!"
