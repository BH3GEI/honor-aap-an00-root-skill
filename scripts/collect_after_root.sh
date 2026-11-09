#!/usr/bin/env bash
# Collect the SELinux and label state that distinguishes a clean boot from a
# post-exploit boot. Read-only apart from one scratch file under
# /data/local/tmp. Root-aware: uses the su client when it is available.
#
#   HONOR_SERIAL=AVXB6R5C19000481 ./scripts/collect_after_root.sh > snap.txt
#
# Compare two snapshots (clean boot, post-exploit) with diff.
set -uo pipefail

SERIAL="${HONOR_SERIAL:-AVXB6R5C19000481}"
ADB=(adb -s "$SERIAL")

run() { "${ADB[@]}" shell "$@" 2>&1; }

# su_ runs a command as root when the daemon is reachable, else as shell.
if "${ADB[@]}" shell '/data/local/tmp/su -c id' 2>/dev/null | grep -q 'uid=0'; then
  su_() { "${ADB[@]}" shell "/data/local/tmp/su -c '$*'"; }
  ROOT=1
else
  su_() { "${ADB[@]}" shell "$*"; }
  ROOT=0
fi

echo "collect_after_root: serial=$SERIAL root=$ROOT"

echo "== identity =="
run 'id; cat /proc/self/attr/current; getenforce; uptime'
echo "== fresh child label =="
run '/system/bin/sh -c "cat /proc/self/attr/current"; /system/bin/sh -c "id"'
echo "== selinux state =="
run 'cat /sys/fs/selinux/enforce; cat /sys/fs/selinux/policyvers; cat /sys/fs/selinux/mls'
for cap in always_check_network cgroup_seclabel extended_socket_class \
           genfs_seclabel_symlinks ioctl_skip_cloexec network_peer_controls \
           nnp_nosuid_transition open_perms userspace_initial_context; do
  printf '%s=' "$cap"
  run "cat /sys/fs/selinux/policy_capabilities/$cap"
done
echo "== labels on disk =="
run 'ls -Zd /data/data /data/local/tmp /system/bin/sh 2>&1'
run 'ls -Z /data/data 2>&1 | head -5'
echo "== label of a newly created inode =="
su_ 'rm -rf /data/local/tmp/seltest_probe; mkdir -p /data/local/tmp/seltest_probe; ls -Zd /data/local/tmp/seltest_probe'
su_ 'touch /data/local/tmp/seltest_probe/f; ls -Z /data/local/tmp/seltest_probe/f'
echo "== denial and policy load history =="
run 'dmesg 2>/dev/null | grep -iE "avc: |selinux|policy" | tail -15'
echo "== app launch probe =="
run 'am start -a android.intent.action.MAIN -c android.intent.category.HOME >/dev/null 2>&1; logcat -d -b crash -t 40 2>/dev/null | tail -25'
echo "collect_after_root: done"
