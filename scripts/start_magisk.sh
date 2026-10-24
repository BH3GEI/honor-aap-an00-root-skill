#!/usr/bin/env bash
set -euo pipefail
SERIAL="${HONOR_SERIAL:-${ANDROID_SERIAL:-}}"
MAGISK_PID=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--serial) SERIAL="$2"; shift 2 ;;
    --magisk-pid) MAGISK_PID="$2"; shift 2 ;;
    *) echo "Usage: $0 [-s SERIAL] [--magisk-pid PID]" >&2; exit 2 ;;
  esac
done
[[ -n "$SERIAL" ]] || { echo "set HONOR_SERIAL or pass --serial" >&2; exit 2; }
ADB="${ADB:-adb}"
SU='/data/local/tmp/su -c'
"$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c id' | grep -q 'uid=0(root)' || {
  echo "temporary root is required; run scripts/run_root.sh first" >&2; exit 3;
}

"$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c "test -x /data/adb/m/magisk"' || {
  echo "missing patched /data/adb/m/magisk; deploy the release binary first" >&2; exit 4;
}

# The stock boot process may restore /data/adb to mode 700.
"$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c "chmod 755 /data/adb /data/adb/m /data/adb/m/.magisk /data/adb/m/.magisk/device; ln -sf magisk /data/adb/m/su"'

if ! "$ADB" -s "$SERIAL" shell '/data/adb/m/magisk -V' >/dev/null 2>&1; then
  "$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c "nohup /data/adb/m/magisk --daemon >/data/local/tmp/magiskd.out 2>&1 &"'
  for _ in 1 2 3 4 5; do
    "$ADB" -s "$SERIAL" shell '/data/adb/m/magisk -V' >/dev/null 2>&1 && break
    "$ADB" -s "$SERIAL" shell 'toybox sleep 1'
  done
fi

APP_UID=$("$ADB" -s "$SERIAL" shell 'dumpsys package com.topjohnwu.magisk 2>/dev/null' | sed -n 's/.*appId=\([0-9][0-9]*\).*/\1/p' | head -1)
if [[ "$APP_UID" =~ ^[0-9]+$ ]]; then
  "$ADB" -s "$SERIAL" shell "/data/local/tmp/su -c \"/data/adb/m/magisk --sqlite \\\"INSERT OR REPLACE INTO policies(uid,policy,until,logging,notification) VALUES ($APP_UID,2,0,1,1),(2000,2,0,1,1)\\\"\"" >/dev/null
fi

if [[ -z "$MAGISK_PID" ]]; then
  MAGISK_PID=$("$ADB" -s "$SERIAL" shell 'pidof com.topjohnwu.magisk' | tr -d '\r' || true)
fi
if [[ "$MAGISK_PID" =~ ^[0-9]+$ ]]; then
  "$ADB" -s "$SERIAL" shell "/data/local/tmp/su -c \"nsenter -t $MAGISK_PID -m -- /system/bin/mount --bind /data/adb/m /debug_ramdisk\"" 2>/dev/null || true
  "$ADB" -s "$SERIAL" shell "/data/local/tmp/su -c \"nsenter -t $MAGISK_PID -m -- /debug_ramdisk/magisk -v\""
else
  "$ADB" -s "$SERIAL" shell '/data/adb/m/magisk -v'
fi
"$ADB" -s "$SERIAL" shell 'PATH=/data/adb/m:$PATH su -c id'
