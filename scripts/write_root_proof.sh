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
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
{
  echo "Honor AAP-AN00 temporary-root proof"
  date -u '+UTC %Y-%m-%d %H:%M:%S'
  "$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c id' | tr -d '\r'
  "$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c getenforce' | tr -d '\r'
} >"$TMP"
grep -q 'uid=0(root)' "$TMP"
"$ADB" -s "$SERIAL" push "$TMP" /sdcard/Download/honor-root-proof.txt >/dev/null
"$ADB" -s "$SERIAL" shell 'cat /sdcard/Download/honor-root-proof.txt'
echo "visible_on_phone=Files > Download > honor-root-proof.txt"
