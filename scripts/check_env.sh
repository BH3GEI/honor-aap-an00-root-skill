#!/usr/bin/env bash
set -euo pipefail

EXPECTED_MODEL="AAP-AN00"
EXPECTED_DISPLAY="AAP-AN00 10.0.0.175(C00E170R104P3)"
EXPECTED_KERNEL="6.12.38-android16-5-gfde7767f6ef6-abogki481467632-4k"
SERIAL="${HONOR_SERIAL:-${ANDROID_SERIAL:-}}"
REQUIRE_TARGET=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--serial) SERIAL="$2"; shift 2 ;;
    --require-target) REQUIRE_TARGET=1; shift ;;
    -h|--help)
      echo "Usage: $0 [-s SERIAL] [--require-target]"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [[ -z "$SERIAL" ]]; then
  echo "set HONOR_SERIAL or pass --serial" >&2; exit 2
fi
ADB="${ADB:-adb}"
command -v "$ADB" >/dev/null || { echo "adb not found" >&2; exit 2; }

echo "[host] os=$(uname -s) arch=$(uname -m) python=$(python3 --version 2>&1 || true)"
echo "[adb] serial=$SERIAL state=$($ADB -s "$SERIAL" get-state 2>/dev/null || echo offline)"
PROP() { "$ADB" -s "$SERIAL" shell "getprop $1" 2>/dev/null | tr -d '\r'; }
MODEL=$(PROP ro.product.model)
DEVICE=$(PROP ro.product.device)
DISPLAY=$(PROP ro.build.display.id)
KERNEL=$("$ADB" -s "$SERIAL" shell uname -r 2>/dev/null | tr -d '\r')
LOCKED=$(PROP ro.boot.flash.locked)
VBSTATE=$(PROP ro.boot.vbmeta.device_state)

cat <<INFO
[device] model=$MODEL device=$DEVICE
[device] display=$DISPLAY
[device] kernel=$KERNEL
[device] flash_locked=$LOCKED vbmeta_state=$VBSTATE
INFO

MISMATCH=0
[[ "$MODEL" == "$EXPECTED_MODEL" ]] || MISMATCH=1
[[ "$DISPLAY" == "$EXPECTED_DISPLAY" ]] || MISMATCH=1
[[ "$KERNEL" == "$EXPECTED_KERNEL" ]] || MISMATCH=1
if [[ "$MISMATCH" -ne 0 ]]; then
  cat <<WARN
[mismatch] supported target:
  model=$EXPECTED_MODEL
  display=$EXPECTED_DISPLAY
  kernel=$EXPECTED_KERNEL
WARN
  if [[ "$REQUIRE_TARGET" -eq 1 ]]; then exit 3; fi
else
  echo "[target] exact supported firmware"
fi

if "$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c id 2>/dev/null' | grep -q 'uid=0(root)'; then
  echo "[root] active"
  "$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c id; /data/local/tmp/su -c getenforce'
else
  echo "[root] not active"
fi
