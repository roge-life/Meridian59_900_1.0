#!/bin/bash
# Meridian 59 Dev Game Server Bootstrap
# Installs deps, downloads latest GitHub Actions artifacts, starts blakserv.
set -euxo pipefail

GH_TOKEN="${github_token}"
GH_ORG="${github_org}"
GH_REPO="${github_repo}"

# --- System Preparation (32-bit) ---
dpkg --add-architecture i386
apt-get update
apt-get install -y \
    libc6:i386 \
    libstdc++6:i386 \
    libmysqlclient21:i386 \
    zlib1g:i386 \
    unzip \
    curl \
    jq

# --- User and Directories ---
useradd -m -s /bin/bash meridian || true
mkdir -p /opt/meridian59/{savegame,channel,rooms,loadkod,memmap,rsc}

# --- Systemd Service ---
cat > /etc/systemd/system/blakserv.service <<'SVCEOF'
[Unit]
Description=Meridian 59 Game Server (Server 900 - Dev)
After=network.target

[Service]
Type=simple
User=meridian
Group=meridian
WorkingDirectory=/opt/meridian59
ExecStart=/opt/meridian59/blakserv -i
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
SVCEOF

systemctl daemon-reload
systemctl enable blakserv

# --- Artifact Download Helper ---
get_artifact_id() {
    local name="$1"
    curl -sf \
        -H "Authorization: Bearer $${GH_TOKEN}" \
        -H "Accept: application/vnd.github+json" \
        -H "X-GitHub-Api-Version: 2022-11-28" \
        "https://api.github.com/repos/$${GH_ORG}/$${GH_REPO}/actions/artifacts?name=$${name}&per_page=1" \
        | jq -r '.artifacts[0].id // empty'
}

download_artifact() {
    local artifact_id="$1"
    local dest="$2"
    curl -sfL \
        -H "Authorization: Bearer $${GH_TOKEN}" \
        -H "Accept: application/vnd.github+json" \
        -H "X-GitHub-Api-Version: 2022-11-28" \
        "https://api.github.com/repos/$${GH_ORG}/$${GH_REPO}/actions/artifacts/$${artifact_id}/zip" \
        -o "$${dest}"
}

# --- Download Server Data (Windows build - contains all game data files) ---
SERVER_ID=$(get_artifact_id "meridian_server")
if [ -z "$${SERVER_ID}" ]; then
    echo "ERROR: meridian_server artifact not found in $${GH_ORG}/$${GH_REPO}" >&2
    exit 1
fi
download_artifact "$${SERVER_ID}" /tmp/meridian_server.zip
unzip -o /tmp/meridian_server.zip -d /opt/meridian59/ -x "blakserv" "*.exe" "*.pdb" "*.dll"

# --- Download Linux Binary (overwrites any Windows binary) ---
LINUX_ID=$(get_artifact_id "blakserv-linux")
if [ -z "$${LINUX_ID}" ]; then
    echo "ERROR: blakserv-linux artifact not found in $${GH_ORG}/$${GH_REPO}" >&2
    exit 1
fi
download_artifact "$${LINUX_ID}" /tmp/blakserv-linux.zip
unzip -o /tmp/blakserv-linux.zip blakserv -d /opt/meridian59/

# --- Permissions ---
chown -R meridian:meridian /opt/meridian59
chmod +x /opt/meridian59/blakserv
chmod 775 /opt/meridian59/{savegame,channel,rsc,loadkod}

systemctl start blakserv

echo "Meridian 59 dev game server bootstrap complete."
