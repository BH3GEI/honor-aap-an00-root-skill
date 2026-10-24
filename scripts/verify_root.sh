#!/usr/bin/env bash
set -euo pipefail
SERIAL="${HONOR_SERIAL:-${ANDROID_SERIAL:-}}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--serial) SERIAL="$2"; shift 2 ;;
    *) echo "Usage: $0 [-s SERIAL]" >&2; exit 2 ;;
  esac
done
[[ -n "$SERIAL" ]] || { echo "set HONOR_SERIAL or pass --serial" >&2; exit 2; }
ADB="${ADB:-adb}"

echo "=== uptime ==="
"$ADB" -s "$SERIAL" shell cat /proc/uptime
echo "=== ordinary shell ==="
"$ADB" -s "$SERIAL" shell id
echo "=== su files ==="
"$ADB" -s "$SERIAL" shell 'ls -l /data/local/tmp/su /data/local/tmp/temp_su.sock /apex/com.android.virt/bin/su 2>/dev/null'
echo "=== root identity ==="
ID=$("$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c id' | tr -d '\r')
echo "$ID"
grep -q 'uid=0(root)' <<<"$ID"
echo "=== SELinux ==="
"$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c getenforce'
