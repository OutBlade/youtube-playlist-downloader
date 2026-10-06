#!/usr/bin/env bash
set -Eeuo pipefail

# Automated bootstrap for an Oracle Cloud Always Free Ampere A1 VM (Ubuntu 22.04 / 24.04 ARM64).
# This configures the fastest free server setup for YouTube Playlist Downloader:
# - Auto-tunes workers and memory limits to utilize all available OCPUs (up to 4 OCPUs and 24 GB RAM)
# - Generates an instant HTTPS domain via sslip.io and automated Caddy SSL
# - Installs an idle-reclamation keepalive daemon so Oracle never deletes the free instance
# - Sets up Docker, UFW firewall, and container services
#
# Run as root:
#   curl -fsSL https://raw.githubusercontent.com/OutBlade/youtube-playlist-downloader/main/deploy/oracle-free-bootstrap.sh -o /tmp/blade-bootstrap.sh
#   sudo bash /tmp/blade-bootstrap.sh

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run this script with sudo: sudo bash $0" >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ca-certificates curl git ufw python3

# Install Docker if missing
if ! command -v docker >/dev/null || ! docker compose version >/dev/null 2>&1; then
  install -m 0755 -d /etc/apt/keyrings
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
  chmod a+r /etc/apt/keyrings/docker.asc
  . /etc/os-release
  printf 'Types: deb\nURIs: https://download.docker.com/linux/ubuntu\nSuites: %s\nComponents: stable\nArchitectures: %s\nSigned-By: /etc/apt/keyrings/docker.asc\n' \
    "${UBUNTU_CODENAME:-$VERSION_CODENAME}" "$(dpkg --print-architecture)" \
    > /etc/apt/sources.list.d/docker.sources
  apt-get update
  apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
fi

systemctl enable --now docker

# Firewall: allow SSH, HTTP (80), HTTPS (443 TCP/UDP for HTTP/3)
ufw allow OpenSSH
ufw allow 80/tcp
ufw allow 443/tcp
ufw allow 443/udp
ufw --force enable

# Clone or update repository
if [[ ! -d /opt/blade/.git ]]; then
  git clone --depth 1 --branch main https://github.com/OutBlade/youtube-playlist-downloader.git /opt/blade
else
  cd /opt/blade
  git pull --quiet origin main || true
fi

# Determine public IPv4
PUBLIC_IPV4="$(curl -fsS --max-time 15 https://api.ipify.org || curl -fsS --max-time 15 https://icanhazip.com)"
if [[ ! "$PUBLIC_IPV4" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
  echo "Could not determine the VM's public IPv4 address." >&2
  exit 1
fi
API_DOMAIN="${PUBLIC_IPV4//./-}.sslip.io"

# Auto-detect hardware resources to maximize download throughput
CORES=$(nproc || echo 2)
TOTAL_RAM_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
TOTAL_RAM_GB=$(( TOTAL_RAM_KB / 1024 / 1024 ))

echo "Detected system resources: $CORES cores, ${TOTAL_RAM_GB} GB RAM"

if [ "$CORES" -ge 4 ] && [ "$TOTAL_RAM_GB" -ge 18 ]; then
  # Full Always Free allocation (4 OCPUs, 24 GB RAM)
  BLADE_CPU_LIMIT=4
  BLADE_MEMORY_LIMIT=20g
  BLADE_WORKERS=16
  BLADE_FRAGMENTS=8
  BLADE_MAX_ACTIVE_JOBS=4
elif [ "$CORES" -ge 2 ]; then
  # Standard 2 OCPU, 12 GB RAM instance
  BLADE_CPU_LIMIT=$CORES
  BLADE_MEMORY_LIMIT=$(( TOTAL_RAM_GB > 3 ? TOTAL_RAM_GB - 2 : 8 ))g
  BLADE_WORKERS=12
  BLADE_FRAGMENTS=8
  BLADE_MAX_ACTIVE_JOBS=2
else
  BLADE_CPU_LIMIT=1
  BLADE_MEMORY_LIMIT=3g
  BLADE_WORKERS=4
  BLADE_FRAGMENTS=4
  BLADE_MAX_ACTIVE_JOBS=1
fi

cat > /opt/blade/.env <<EOF
API_DOMAIN=$API_DOMAIN
BLADE_MEMORY_LIMIT=$BLADE_MEMORY_LIMIT
BLADE_CPU_LIMIT=$BLADE_CPU_LIMIT
BLADE_WORKERS=$BLADE_WORKERS
BLADE_FRAGMENTS=$BLADE_FRAGMENTS
BLADE_MAX_ACTIVE_JOBS=$BLADE_MAX_ACTIVE_JOBS
BLADE_QUEUE_LIMIT=100
EOF
chmod 600 /opt/blade/.env
chown -R root:root /opt/blade

# Install Oracle idle-reclamation keepalive service
cat > /etc/systemd/system/blade-keepalive.service <<EOF
[Unit]
Description=Oracle Cloud Always Free Keepalive Daemon
After=network.target

[Service]
Type=simple
User=root
ExecStart=/usr/bin/python3 /opt/blade/deploy/oracle-keepalive.py
Restart=always
RestartSec=10
Nice=19
CPUSchedulingPolicy=idle

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now blade-keepalive.service

# Launch server and reverse proxy
cd /opt/blade
docker compose -f compose.production.yaml --env-file .env config --quiet
docker compose -f compose.production.yaml --env-file .env up --build -d

echo "Waiting for public HTTPS API at https://$API_DOMAIN/api/health"
for attempt in $(seq 1 60); do
  if curl -fsS --max-time 10 "https://$API_DOMAIN/api/health" 2>/dev/null | grep -q '"ready":true'; then
    printf '\n\n============================================================\n'
    printf 'SUCCESS: Downloader API is live at https://%s\n' "$API_DOMAIN"
    printf 'Speed Profile: %s cores, %s RAM, %s workers, %s max jobs\n' \
      "$BLADE_CPU_LIMIT" "$BLADE_MEMORY_LIMIT" "$BLADE_WORKERS" "$BLADE_MAX_ACTIVE_JOBS"
    printf 'Keepalive: Active (protects against Oracle 7-day idle reclamation)\n'
    printf '============================================================\n\n'
    
    echo "To connect your GitHub Pages website (https://outblade.github.io/youtube-playlist-downloader/):"
    echo ""
    echo "From this Linux server (if git push is configured):"
    echo "  /opt/blade/deploy/publish-backend.sh https://$API_DOMAIN"
    echo ""
    echo "Or from your Windows checkout:"
    echo "  .\\deploy\\publish-backend.ps1 -Address https://$API_DOMAIN"
    echo ""
    exit 0
  fi
  sleep 10
done

echo "The containers started, but public HTTPS did not become ready in time."
echo "Check OCI ingress rules for TCP ports 80 and 443 in your VCN Security List, then inspect logs with:"
echo "cd /opt/blade && docker compose -f compose.production.yaml --env-file .env logs --tail=100"
exit 1
