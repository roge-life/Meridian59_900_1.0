#!/bin/bash
# Meridian 59 Dev Web API Bootstrap
# Installs MariaDB + Python + nginx, clones repo, configures and starts the FastAPI app.
set -euxo pipefail

GH_TOKEN="${github_token}"
GH_ORG="${github_org}"
GH_REPO="${github_repo}"
DB_PASSWORD="${db_password}"
GAME_SERVER_IP="${game_server_ip}"
DOMAIN="${domain}"
APP_SECRET="${secret_key}"

# --- Wait for apt lock ---
while fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1; do
    echo "Waiting for apt lock..."
    sleep 5
done

# --- System Update ---
apt-get update
apt-get upgrade -y

# --- Install Packages ---
apt-get install -y \
    mariadb-server \
    mariadb-client \
    python3-pip \
    python3-venv \
    python3-dev \
    libmariadb-dev \
    pkg-config \
    git \
    curl \
    nginx \
    certbot \
    python3-certbot-nginx

# --- MariaDB Setup ---
systemctl enable --now mariadb

mysql -e "CREATE DATABASE IF NOT EXISTS m59_web CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
mysql -e "CREATE USER IF NOT EXISTS 'm59api'@'localhost' IDENTIFIED BY '$${DB_PASSWORD}';"
mysql -e "GRANT ALL PRIVILEGES ON m59_web.* TO 'm59api'@'localhost';"
mysql -e "FLUSH PRIVILEGES;"

# --- Clone Repo ---
git clone "https://$${GH_TOKEN}@github.com/$${GH_ORG}/$${GH_REPO}.git" /tmp/m59-repo
mkdir -p /opt/m59-account-api
cp -r /tmp/m59-repo/webapi/. /opt/m59-account-api/
rm -rf /tmp/m59-repo

# --- Patch main.py to read M59_SERVER_IP and DATABASE_URL from env ---
sed -i 's|M59_SERVER_IP = ".*"|M59_SERVER_IP = os.getenv("M59_SERVER_IP", "127.0.0.1")|' \
    /opt/m59-account-api/main.py
sed -i 's|DATABASE_URL = ".*"|DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./fallback.db")|' \
    /opt/m59-account-api/main.py

# --- Write .env file ---
cat > /opt/m59-account-api/.env <<'ENVEOF'
M59_SERVER_IP=${game_server_ip}
DATABASE_URL=mysql+pymysql://m59api:${db_password}@localhost/m59_web
SECRET_KEY=${secret_key}
ENVEOF

# --- Python venv + deps ---
cd /opt/m59-account-api
python3 -m venv venv
./venv/bin/pip install --upgrade pip
./venv/bin/pip install -r requirements.txt

chown -R www-data:www-data /opt/m59-account-api

# --- Systemd service for uvicorn ---
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
ExecStart=/opt/m59-account-api/venv/bin/uvicorn main:app --host 127.0.0.1 --port 8000
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
SVCEOF

systemctl daemon-reload
systemctl enable --now m59-webapi

# --- Nginx reverse proxy ---
rm -f /etc/nginx/sites-enabled/default

cat > /etc/nginx/sites-available/m59-webapi <<'NGINXEOF'
server {
    listen 80;
    server_name ${domain};

    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
NGINXEOF

ln -sf /etc/nginx/sites-available/m59-webapi /etc/nginx/sites-enabled/
nginx -t
systemctl enable --now nginx

echo "Meridian 59 dev web API bootstrap complete."
echo "Next: point DNS ${domain} -> $(curl -sf http://169.254.169.254/metadata/v1/interfaces/public/0/ipv4/address), then: certbot --nginx -d ${domain}"
