#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
URL="https://github.com/Linuxoid-cn/CVE-2026-43499-Poc-Analysis.git"
COMMIT="d7e805fc1a13b87ecf644a8869e6d313f2d1ecde"
DEST="vendor/linuxoid"

if [[ ! -d "$DEST/.git" ]]; then
  mkdir -p vendor
  git clone --filter=blob:none "$URL" "$DEST"
fi
git -C "$DEST" checkout --detach "$COMMIT"
test -f "$DEST/private_key.json"
echo "upstream_ready=$DEST@$COMMIT"
