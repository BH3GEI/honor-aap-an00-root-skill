#!/usr/bin/env bash
set -euo pipefail
SERIAL="${HONOR_SERIAL:-${ANDROID_SERIAL:-}}"
APK=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--serial) SERIAL="$2"; shift 2 ;;
    --apk) APK="$2"; shift 2 ;;
    *) echo "Usage: $0 -s SERIAL --apk /path/to/Magisk-refresh.apk" >&2; exit 2 ;;
  esac
done
[[ -n "$SERIAL" && -n "$APK" ]] || {
  echo "Usage: $0 -s SERIAL --apk /path/to/Magisk-refresh.apk" >&2; exit 2;
}
[[ -f "$APK" ]] || { echo "APK not found: $APK" >&2; exit 2; }
ADB="${ADB:-adb}"
"$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c id' | grep -q 'uid=0(root)' || {
  echo "temporary root is required for the Honor install-confirmation workaround" >&2; exit 3;
}
"$ADB" -s "$SERIAL" push "$APK" /data/local/tmp/Magisk-refresh.apk >/dev/null

"$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c "runcon u:r:shell:s0 settings put secure adb_install_need_confirm 0"'
RESTORE=0
cleanup() {
  if [[ "$RESTORE" -eq 0 ]]; then
    "$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c "runcon u:r:shell:s0 settings put secure adb_install_need_confirm 1"' >/dev/null 2>&1 || true
    RESTORE=1
  fi
}
trap cleanup EXIT
if ! "$ADB" -s "$SERIAL" shell '/data/local/tmp/su -c "runcon u:r:shell:s0 pm install -r /data/local/tmp/Magisk-refresh.apk"'; then
  echo "install failed; a different Magisk signature may require uninstalling the older app first" >&2
  exit 4
fi
"$ADB" -s "$SERIAL" shell 'pm path com.topjohnwu.magisk'
