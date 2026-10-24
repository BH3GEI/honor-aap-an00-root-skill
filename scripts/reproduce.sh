#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SERIAL="${HONOR_SERIAL:-${ANDROID_SERIAL:-}}"
INSTALL_MAGISK=1
REPLACE_MAGISK=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--serial) SERIAL="$2"; shift 2 ;;
    --no-magisk) INSTALL_MAGISK=0; shift ;;
    --replace-magisk) REPLACE_MAGISK=1; shift ;;
    -h|--help)
      echo "Usage: $0 [-s SERIAL] [--no-magisk] [--replace-magisk]"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ -n "$SERIAL" ]] || { echo "set HONOR_SERIAL first, or pass --serial" >&2; exit 2; }
export HONOR_SERIAL="$SERIAL"

echo "=== 1/6 download and verify artifacts ==="
python3 scripts/download_artifacts.py

echo "=== 2/6 check exact device and firmware ==="
./scripts/check_env.sh --require-target

echo "=== 3/6 temporary root ==="
./scripts/run_root.sh

echo "=== 4/6 independent verification ==="
./scripts/verify_root.sh
./scripts/write_root_proof.sh

if [[ "$INSTALL_MAGISK" -eq 1 ]]; then
  echo "=== 5/6 install Magisk app ==="
  if [[ "$REPLACE_MAGISK" -eq 1 ]]; then
    adb -s "$SERIAL" shell '/data/local/tmp/su -c "runcon u:r:shell:s0 pm uninstall com.topjohnwu.magisk"'
  fi
  ./scripts/install_magisk_app.sh --apk magisk-runtime/Magisk-refresh.apk

  echo "=== 6/6 deploy and start Magisk daemon ==="
  adb -s "$SERIAL" push magisk-runtime/magisk /data/local/tmp/magisk-bin >/dev/null
  adb -s "$SERIAL" shell '/data/local/tmp/su -c "mkdir -p /data/adb/m/.magisk/device && cp /data/local/tmp/magisk-bin /data/adb/m/magisk.new && mv /data/adb/m/magisk.new /data/adb/m/magisk && chmod 755 /data/adb/m/magisk"'
  ./scripts/start_magisk.sh
else
  echo "=== Magisk skipped ==="
fi

cat <<'DONE'
 reproduction complete
 - root: /data/local/tmp/su
 - Magisk CLI: PATH=/data/adb/m:$PATH su -c id
 - phone proof: Files > Download > honor-root-proof.txt
 - after reboot: run this script again
DONE
