#!/usr/bin/env bash
set -Eeuo pipefail

# Publishes the running backend server address to the repository's 'backend' branch
# so the GitHub Pages frontend at https://outblade.github.io/youtube-playlist-downloader/
# knows where to route download requests.
#
# Usage:
#   ./deploy/publish-backend.sh https://YOUR_API_DOMAIN

ADDRESS="${1:-}"

if [[ -z "$ADDRESS" ]]; then
  echo "Usage: $0 https://YOUR_API_DOMAIN" >&2
  exit 1
fi

# Trim any trailing slashes
ADDRESS="${ADDRESS%/}"

if [[ ! "$ADDRESS" =~ ^https://[a-zA-Z0-9.-]+(:[0-9]+)?$ ]]; then
  echo "Error: Address must be a valid HTTPS URL (e.g. https://123-45-67-89.sslip.io)" >&2
  exit 1
fi

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_DIR"

echo "Checking backend health at $ADDRESS/api/health..."
HEALTH=$(curl -fsS --max-time 15 "$ADDRESS/api/health" || echo "")
if [[ ! "$HEALTH" =~ \"ready\":\ *true ]]; then
  echo "Error: The backend at $ADDRESS is not reporting ready." >&2
  echo "Received: $HEALTH" >&2
  exit 1
fi

echo "API is ready. Publishing to 'backend' branch..."

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

echo "{\"url\":\"$ADDRESS\"}" > "$TMP_DIR/backend.json"
BLOB=$(git -c core.autocrlf=false hash-object -w "$TMP_DIR/backend.json")

export GIT_INDEX_FILE="$TMP_DIR/index"
git update-index --add --cacheinfo "100644,$BLOB,backend.json"
TREE=$(git write-tree)
unset GIT_INDEX_FILE

COMMIT=$(git commit-tree "$TREE" -m "Publish server address: $ADDRESS")
git push --quiet --force origin "${COMMIT}:refs/heads/backend"

echo "Successfully published $ADDRESS."
echo "Visit https://outblade.github.io/youtube-playlist-downloader/ to use your live downloader."
