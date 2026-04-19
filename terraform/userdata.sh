#!/bin/bash
# Meridian 59 Server 900 Bootstrap Script

set -e

# 1. System Preparation (32-bit architecture)
dpkg --add-architecture i386
apt-get update
apt-get install -y \
    libc6:i386 \
    libstdc++6:i386 \
    libmysqlclient21:i386 \
    libmariadb-dev-compat:i386 \
    zlib1g:i386 \
    unzip \
    wget

# 2. Create Meridian User and Directory Structure
useradd -m -s /bin/bash meridian
mkdir -p /opt/meridian59/{savegame,channel,rooms,loadkod,memmap,rsc}

# 3. Create Systemd Service File
cat > /etc/systemd/system/blakserv.service <<EOF
[Unit]
Description=Meridian 59 Game Server (Server 900)
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
EOF

# 4. Final Permissions
chown -R meridian:meridian /opt/meridian59
systemctl daemon-reload
systemctl enable blakserv

echo "Meridian 59 bootstrap complete. Ready for binary deployment."
