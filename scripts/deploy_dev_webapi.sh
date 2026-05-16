#!/bin/bash
# Deploy the FastAPI web portal to the dev web server.
# Uses `gh` CLI for auth. Reads DO token from ~/do_token.txt for terraform output.
set -euo pipefail

REPO="roge-life/Meridian59_900_1.0"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WEB_HOST="dev.emfiftynine.info"
GAME_HOST="901.emfiftynine.info"
DOMAIN="dev.emfiftynine.info"

DO_TOKEN=$(tr -d '[:space:]' < "$HOME/do_token.txt")
GH_TOKEN=$(gh auth token)

if [[ -z "${GOOGLE_CLIENT_ID:-}" || -z "${GOOGLE_CLIENT_SECRET:-}" ]]; then
    echo "ERROR: GOOGLE_CLIENT_ID and GOOGLE_CLIENT_SECRET must be set in environment" >&2
    exit 1
fi

echo "==> Reading generated secrets from Terraform state"
DB_PASSWORD=$(cd "$REPO_ROOT/terraform-dev" && TF_VAR_do_token="$DO_TOKEN" TF_VAR_github_token="$GH_TOKEN" terraform output -raw db_password)
SECRET_KEY=$(cd   "$REPO_ROOT/terraform-dev" && TF_VAR_do_token="$DO_TOKEN" TF_VAR_github_token="$GH_TOKEN" terraform output -raw secret_key)
GAME_SERVER_IP=$(getent hosts "$GAME_HOST" | awk '{print $1}' | head -1)

echo "==> Deploying web API to $WEB_HOST (game server: $GAME_SERVER_IP)"

ssh -o StrictHostKeyChecking=no "root@$WEB_HOST" bash -s <<REMOTE
set -euo pipefail
DB_PASSWORD="$DB_PASSWORD"
SECRET_KEY="$SECRET_KEY"
GAME_SERVER_IP="$GAME_SERVER_IP"
DOMAIN="$DOMAIN"
GH_TOKEN="$GH_TOKEN"

while fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1; do sleep 2; done
apt-get update -qq
apt-get install -y mariadb-server mariadb-client python3-pip python3-venv python3-dev \
    libmariadb-dev pkg-config git curl nginx certbot python3-certbot-nginx dnsutils

systemctl enable --now mariadb

# portal DB + user
mysql -e "CREATE DATABASE IF NOT EXISTS m59_web CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
mysql -e "CREATE USER IF NOT EXISTS 'm59api'@'localhost' IDENTIFIED BY '\$DB_PASSWORD';"
mysql -e "GRANT ALL PRIVILEGES ON m59_web.* TO 'm59api'@'localhost';"

# blakserv analytics DB + read-only user (blakserv writes here; admin panel reads it)
mysql -e "CREATE DATABASE IF NOT EXISTS blakserv CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
mysql -e "CREATE USER IF NOT EXISTS 'm59reader'@'localhost' IDENTIFIED BY 'm59read_pass';"
mysql -e "GRANT SELECT ON blakserv.* TO 'm59reader'@'localhost';"
mysql -e "FLUSH PRIVILEGES;"

if [ -d /opt/m59-account-api/.git ]; then
    git -C /opt/m59-account-api pull
else
    rm -rf /opt/m59-account-api
    git clone --depth 1 --branch fix-init-experience "https://\$GH_TOKEN@github.com/$REPO.git" /tmp/m59-repo
    mkdir -p /opt/m59-account-api
    cp -r /tmp/m59-repo/webapi/. /opt/m59-account-api/
    rm -rf /tmp/m59-repo
fi

# app/main.py already uses os.getenv() — no patching needed
cat > /opt/m59-account-api/.env <<ENVEOF
M59_SERVER_IP=\$GAME_SERVER_IP
M59_MAINTENANCE_PORT=9999
DATABASE_URL=mysql+pymysql://m59api:\$DB_PASSWORD@localhost/m59_web
BLAKSERV_DATABASE_URL=mysql+pymysql://m59reader:m59read_pass@localhost/blakserv
SECRET_KEY=\$SECRET_KEY
FORCE_HTTPS=1
GOOGLE_CLIENT_ID=$GOOGLE_CLIENT_ID
GOOGLE_CLIENT_SECRET=$GOOGLE_CLIENT_SECRET
ENVEOF

cd /opt/m59-account-api
python3 -m venv venv
./venv/bin/pip install --upgrade pip -q
./venv/bin/pip install -r requirements.txt -q
chown -R www-data:www-data /opt/m59-account-api

cat > /etc/systemd/system/m59-webapi.service <<'SVCEOF'
[Unit]
Description=Meridian 59 Web API (Dev)
After=network.target mariadb.service

[Service]
Type=simple
User=www-data
Group=www-data
WorkingDirectory=/opt/m59-account-api
EnvironmentFile=/opt/m59-account-api/.env
ExecStart=/opt/m59-account-api/venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8000
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
SVCEOF

systemctl daemon-reload
systemctl enable m59-webapi
systemctl restart m59-webapi

rm -f /etc/nginx/sites-enabled/default
cat > /etc/nginx/sites-available/m59-webapi <<NGINXEOF
server {
    listen 80;
    server_name \$DOMAIN;

    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host \\\$host;
        proxy_set_header X-Real-IP \\\$remote_addr;
        proxy_set_header X-Forwarded-For \\\$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \\\$scheme;
    }
}
NGINXEOF

ln -sf /etc/nginx/sites-available/m59-webapi /etc/nginx/sites-enabled/
nginx -t && systemctl enable --now nginx && systemctl reload nginx

certbot --nginx -d "$DOMAIN" --non-interactive --agree-tos -m joel.palmtag@gmail.com --redirect

systemctl status m59-webapi --no-pager
REMOTE

echo "==> Web API deploy complete"
echo "==> Enable HTTPS: ssh root@$WEB_HOST 'certbot --nginx -d $DOMAIN --non-interactive --agree-tos -m joel.palmtag@gmail.com'"
