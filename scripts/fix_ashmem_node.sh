#!/usr/bin/env bash
# The exploit uses random_boot_id_data as the read-back oracle of its direct
# read primitive, so after a run the kernel boot UUID contains leftover kernel
# pointers. libcutils builds the ashmem device path as "/dev/ashmem" + boot_id;
# the node for the corrupted UUID does not exist, and every NEW app process
# dies in bindApplication with "Unable to open ashmem device".
#
# Fix: create a matching /dev/ashmem<boot_id> node (same 10:124 device as the
# plain node). Writing /proc/sys/kernel/random/boot_id back is not possible on
# this kernel (sysctl is 0444 and proc_sys_permission rejects write even for
# root with CAP_DAC_OVERRIDE).
#
# Usage: fix_ashmem_node.sh [-s SERIAL]
set -euo pipefail
SERIAL="${HONOR_SERIAL:-${ANDROID_SERIAL:-}}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    -s|--serial) SERIAL="$2"; shift 2 ;;
    -h|--help) echo "Usage: $0 [-s SERIAL]"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ -n "$SERIAL" ]] || { echo "Usage: $0 -s SERIAL" >&2; exit 2; }
ADB="${ADB:-adb}"

BID="$($ADB -s "$SERIAL" shell "su -c 'cat /proc/sys/kernel/random/boot_id'" \
  | tr -d ' \r\n')"
[[ "$BID" =~ ^[0-9a-fA-F-]{36}$ ]] || {
  echo "boot_id not readable or malformed: '$BID'" >&2
  exit 1
}

NODE="/dev/ashmem$BID"
"$ADB" -s "$SERIAL" shell "su -c '[ -e $NODE ] || mknod $NODE c 10 124'" \
  > /dev/null 2>&1 || true
"$ADB" -s "$SERIAL" shell "su -c 'chmod 666 $NODE'" > /dev/null 2>&1 || true
if "$ADB" -s "$SERIAL" shell "su -c 'head -c 0 $NODE'" > /dev/null 2>&1; then
  echo "ashmem node ready: $NODE"
else
  echo "failed to create/open $NODE" >&2
  exit 1
fi
