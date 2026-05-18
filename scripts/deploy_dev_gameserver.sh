#!/bin/bash
# Deploy blakserv + game data to the dev game server.
# Uses `gh` CLI (already authenticated) to download artifacts, then rsyncs to server.
set -euo pipefail

REPO="roge-life/Meridian59_900_1.0"
GAME_HOST="901.emfiftynine.info"
DB_HOST_NAME="dev.emfiftynine.info"
STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT

echo "==> Fetching latest artifact run IDs from $REPO"
WINDOWS_RUN=$(gh run list --repo "$REPO" --workflow "buildonpush.yml" --status success --limit 1 --json databaseId -q '.[0].databaseId')
LINUX_RUN=$(gh run list   --repo "$REPO" --workflow "build-linux.yml" --status success --limit 1 --json databaseId -q '.[0].databaseId')

echo "    Windows run: $WINDOWS_RUN  Linux run: $LINUX_RUN"

echo "==> Downloading meridian_server artifact"
gh run download "$WINDOWS_RUN" --repo "$REPO" --name meridian_server --dir "$STAGING/server"

echo "==> Downloading blakserv-linux artifact"
gh run download "$LINUX_RUN" --repo "$REPO" --name blakserv-linux --dir "$STAGING/linux"

# Linux binary overwrites whatever the Windows build put in the server dir
cp "$STAGING/linux/blakserv" "$STAGING/server/blakserv"

echo "==> Setting up service on $GAME_HOST"
ssh -o StrictHostKeyChecking=no "root@$GAME_HOST" bash -s <<'REMOTE'
set -euo pipefail

dpkg --add-architecture i386
apt-get update -qq
apt-get install -y libc6:i386 libstdc++6:i386 libmysqlclient21:i386 zlib1g:i386

useradd -m -s /bin/bash meridian 2>/dev/null || true
mkdir -p /opt/meridian59/{savegame,channel,rooms,loadkod,memmap,rsc}

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
REMOTE

echo "==> Saving game state before restart"
if ssh -o StrictHostKeyChecking=no "root@$GAME_HOST" 'systemctl is-active --quiet blakserv'; then
    echo "    blakserv is running — saving..."
    ssh -o StrictHostKeyChecking=no "root@$GAME_HOST" \
        'curl -sf -X POST http://127.0.0.1:9999/execute \
            -H "Content-Type: application/json" \
            -d "{\"command\":\"save game\"}" && sleep 5' \
        || { echo "ERROR: save game failed — aborting to protect player data"; exit 1; }
    echo "    save complete"
else
    echo "    blakserv not running — skipping save"
fi

echo "==> Syncing game files to $GAME_HOST"
rsync -rlpt --delete \
    --exclude '*.exe' --exclude '*.pdb' --exclude '*.dll' \
    --exclude 'channel/*' --exclude 'savegame/*' --exclude '*.log' \
    "$STAGING/server/" "root@$GAME_HOST:/opt/meridian59/"

echo "==> Patching blakserv.cfg for dev environment"
DB_HOST=$(getent hosts "$DB_HOST_NAME" | awk '{print $1}' | head -1)
ssh -o StrictHostKeyChecking=no "root@$GAME_HOST" bash -s <<REMOTE
sed -i 's/^Enabled.*/Enabled     Yes/' /opt/meridian59/blakserv.cfg
sed -i 's/^Host.*/Host        $DB_HOST/' /opt/meridian59/blakserv.cfg
grep -A5 '\[MySQL\]' /opt/meridian59/blakserv.cfg

# Patch [Login] MinClassicVersion and [Update] section using Python so we
# can insert keys that don't exist in the artifact's blank section.
python3 - <<'PYEOF'
import re

with open('/opt/meridian59/blakserv.cfg', 'rb') as f:
    raw = f.read()

# Work in text (strip \r so regex is simple, we'll write LF-only which the parser handles)
text = raw.decode('utf-8', errors='replace').replace('\r\n', '\n').replace('\r', '\n')

# Patch [Login] MinClassicVersion (replace existing or append inside section)
if re.search(r'^MinClassicVersion', text, re.MULTILINE):
    text = re.sub(r'^MinClassicVersion.*', 'MinClassicVersion    5078', text, flags=re.MULTILINE)
else:
    text = re.sub(r'(\[Login\][^\[]*)', r'\1MinClassicVersion    5078\n', text, flags=re.DOTALL)

# Replace entire [Update] section content (everything between [Update] and next [Section])
update_block = (
    "[Update]\n"
    "DownloadReason       <An update is available. Your client will now update.>\n"
    "ClassicPatchHost     dev.emfiftynine.info\n"
    "ClassicPatchPath     /patch\n"
    "ClassicPatchCachePath /patch/\n"
    "ClassicPatchTxt      patchinfo.txt\n"
    "ClassicClubExe       club.exe\n"
)
text = re.sub(r'\[Update\][^\[]*', update_block, text, flags=re.DOTALL)

with open('/opt/meridian59/blakserv.cfg', 'w', newline='\n') as f:
    f.write(text)

print("Config patched OK")
PYEOF

grep -A8 '\[Update\]' /opt/meridian59/blakserv.cfg
grep 'MinClassicVersion' /opt/meridian59/blakserv.cfg
REMOTE

echo "==> Fixing permissions and starting blakserv"
ssh -o StrictHostKeyChecking=no "root@$GAME_HOST" bash -s <<'REMOTE'
mkdir -p /opt/meridian59/{savegame,channel,rsc,loadkod}
chown -R meridian:meridian /opt/meridian59
chmod +x /opt/meridian59/blakserv
chmod 775 /opt/meridian59/{savegame,channel,rsc,loadkod}
systemctl restart blakserv
REMOTE

echo "==> Deploying HTTP bridge (port 9999 -> blakserv TCP 9998)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ssh -o StrictHostKeyChecking=no "root@$GAME_HOST" "mkdir -p /opt/m59-bridge"
rsync -lpt "$SCRIPT_DIR/../webapi/bridge/bridge.py" "root@$GAME_HOST:/opt/m59-bridge/bridge.py"

ssh -o StrictHostKeyChecking=no "root@$GAME_HOST" bash -s <<'REMOTE'
chown -R meridian:meridian /opt/m59-bridge

cat > /etc/systemd/system/m59-bridge.service <<'SVCEOF'
[Unit]
Description=M59 HTTP Bridge (port 9999)
After=blakserv.service

[Service]
Type=simple
User=meridian
Group=meridian
ExecStart=/usr/bin/python3 /opt/m59-bridge/bridge.py
Restart=on-failure
RestartSec=3s
Environment=M59_HOST=127.0.0.1
Environment=M59_MAINTENANCE_PORT=9998
Environment=BRIDGE_PORT=9999

[Install]
WantedBy=multi-user.target
SVCEOF

systemctl daemon-reload
systemctl enable --now m59-bridge
sleep 2
systemctl status blakserv m59-bridge --no-pager
REMOTE

echo "==> Game server deploy complete"
