#!/usr/bin/env bash
set -Eeuo pipefail

# One-time bootstrap for an Oracle Cloud Always Free Ampere A1 VM running Ubuntu.
# Run as root: sudo bash oracle-free-bootstrap.sh
# It deliberately stays within 2 OCPUs and 12 GB RAM.

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run this script with sudo: sudo bash $0" >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ca-certificates curl git ufw

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

# OCI's security list/network firewall must also allow public TCP 80 and 443.
ufw allow OpenSSH
ufw allow 80/tcp
ufw allow 443/tcp
ufw --force enable

if [[ ! -d /opt/blade/.git ]]; then
  git clone --depth 1 --branch main https://github.com/OutBlade/youtube-playlist-downloader.git /opt/blade
fi

PUBLIC_IPV4="$(curl -fsS --max-time 15 https://api.ipify.org)"
if [[ ! "$PUBLIC_IPV4" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
  echo "Could not determine the VM's public IPv4 address." >&2
  exit 1
fi
API_DOMAIN="${PUBLIC_IPV4//./-}.sslip.io"

cat > /opt/blade/.env <<EOF
API_DOMAIN=$API_DOMAIN
BLADE_MEMORY_LIMIT=10g
BLADE_CPU_LIMIT=2
BLADE_WORKERS=2
BLADE_FRAGMENTS=4
BLADE_MAX_ACTIVE_JOBS=1
BLADE_QUEUE_LIMIT=100
EOF
chmod 600 /opt/blade/.env
chown -R root:root /opt/blade

cd /opt/blade
docker compose -f compose.production.yaml --env-file .env config --quiet
docker compose -f compose.production.yaml --env-file .env up --build -d

echo "Waiting for the public HTTPS API at https://$API_DOMAIN/api/health"
for attempt in $(seq 1 60); do
  if curl -fsS --max-time 10 "https://$API_DOMAIN/api/health"; then
    printf '\n\nAPI_DOMAIN=https://%s\n' "$API_DOMAIN"
    echo "Publish this API address from your Windows checkout with:"
    echo ".\\deploy\\publish-backend.ps1 -Address https://$API_DOMAIN"
    exit 0
  fi
  sleep 10
done

echo "The containers started, but public HTTPS did not become ready. Check OCI ingress rules for TCP ports 80 and 443, then inspect:"
echo "cd /opt/blade && docker compose -f compose.production.yaml --env-file .env logs --tail=100"
exit 1
