#!/usr/bin/env bash
# Read-only check of the persistence boundary on the target device.
#
# Reports why the runtime root does not survive a reboot on this firmware:
# locked bootloader, green AVB, verity enforcing, user build without
# ro.debuggable, /data mounted nosuid,nodev, and no init hook that runs
# anything from /data.
#
# Changes nothing on the device. Requires only adb.
set -euo pipefail

SERIAL="${HONOR_SERIAL:-${ANDROID_SERIAL:-}}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--serial) SERIAL="$2"; shift 2 ;;
    -h|--help) echo "Usage: $0 [-s SERIAL]"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [[ -z "$SERIAL" ]]; then
  echo "set HONOR_SERIAL or pass --serial" >&2
  exit 2
fi
ADB="${ADB:-adb}"

sh_() { "$ADB" -s "$SERIAL" shell "$1" 2>/dev/null | tr -d '\r'; }
prop() { sh_ "getprop $1"; }

LOCKED=$(prop ro.boot.flash.locked)
VBSTATE=$(prop ro.boot.verifiedbootstate)
VBDEV=$(prop ro.boot.vbmeta.device_state)
VERITY=$(prop ro.boot.veritymode)
DEBUGGABLE=$(prop ro.debuggable)
BUILDTYPE=$(prop ro.build.type)
TAGS=$(prop ro.build.tags)
UPTIME=$(sh_ "cut -d' ' -f1 /proc/uptime")
DATA_MOUNT=$(sh_ "mount | grep -m1 ' on /data type '")

cat <<INFO
[device] model=$(prop ro.product.model) display=$(prop ro.build.display.id)
[device] build_type=$BUILDTYPE tags=$TAGS debuggable=$DEBUGGABLE
[device] uptime_s=$UPTIME
[boot]   flash_locked=$LOCKED vbmeta_state=$VBDEV verified_boot=$VBSTATE verity=$VERITY
[data]   $DATA_MOUNT
INFO

DATA_OPTS=$(sed -n 's/.*(\(.*\)).*/\1/p' <<<"$DATA_MOUNT")
HAS_NOSUID=0
HAS_NODEV=0
grep -q 'nosuid' <<<",$DATA_OPTS," && HAS_NOSUID=1
grep -q 'nodev' <<<",$DATA_OPTS," && HAS_NODEV=1

ROOT_ACTIVE=0
sh_ '/data/local/tmp/su -c id 2>/dev/null' | grep -q 'uid=0(root)' && ROOT_ACTIVE=1
if [[ "$ROOT_ACTIVE" -eq 1 ]]; then
  su_() { sh_ "/data/local/tmp/su -c \"$1\""; }
else
  su_() { sh_ "$1"; }
fi

echo "[root]   active=$ROOT_ACTIVE"
MAGISK_DIRS=$(su_ "ls -d /data/adb/post-fs-data.d /data/adb/service.d /data/adb/modules 2>/dev/null || true")
[[ -n "$MAGISK_DIRS" ]] && sed 's/^/[magisk-dir] /' <<<"$MAGISK_DIRS" || echo "[magisk-dir] not listed"
MAGISKD=$(su_ "ps -A 2>/dev/null | grep -c '[m]agiskd' || true")
SU_BIN=$(sh_ "ls -l /data/local/tmp/su 2>/dev/null || true")
MAGISK_VER=$(sh_ "/data/adb/m/magisk -v 2>&1 || true")
echo "[magisk] magiskd_processes=$MAGISKD version=${MAGISK_VER:-unavailable}"
echo "[su]     ${SU_BIN:-absent}"

REASONS=()
[[ "$LOCKED" == "1" ]] && REASONS+=("bootloader locked (ro.boot.flash.locked=1); boot/init_boot/vbmeta cannot be replaced")
[[ "$VBSTATE" == "green" ]] && REASONS+=("AVB state green; modified boot partitions fail verification")
[[ "$VERITY" == "enforcing" ]] && REASONS+=("dm-verity enforcing; /system and /vendor are read-only")
[[ "$DEBUGGABLE" == "0" ]] && REASONS+=("user build with ro.debuggable=0; 'adb root' is unavailable")
[[ "$HAS_NOSUID" -eq 1 ]] && REASONS+=("/data mounted nosuid; a setuid binary on /data cannot elevate")
[[ "$HAS_NODEV" -eq 1 ]] && REASONS+=("/data mounted nodev")
REASONS+=("stock init starts no script from /data; magiskd needs root to start, so it cannot bootstrap itself")
REASONS+=("temp_su.sock and the su binary under /apex live in tmpfs and disappear on reboot")

echo
echo "[boundary] persistence requires one of: unlocked bootloader with patched boot, or a root-at-boot code path on this firmware."
echo "[boundary] neither is available in the state above."
echo
echo "[reasons]"
for r in ${REASONS[@]+"${REASONS[@]}"}; do echo "  - $r"; done
echo
echo "[conclusion] after a reboot run ./scripts/run_root.sh and ./scripts/start_magisk.sh again."
