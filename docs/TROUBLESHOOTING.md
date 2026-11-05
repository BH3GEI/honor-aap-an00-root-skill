# Troubleshooting

## Unsupported firmware

**Message:** `mismatch` or `check_env.sh` exits with status 3.

**Action:** Do not run the supplied payload. Confirm model, display build, and `uname -r`. A different firmware requires new offsets and independent validation.

## Missing upstream signing material

**Message:** `challenge key not found` or `run scripts/fetch_upstream.sh first`.

**Action:**

```bash
./scripts/fetch_upstream.sh
```

The expected path is `vendor/linuxoid/private_key.json`. Do not commit or print its contents.

## Payload already rooted the device

**Message:** `[root] already active; exploit not rerun`.

**Action:** Use `scripts/verify_root.sh`. This check prevents an unnecessary second exploit attempt.

## `slide ktext calibration failed`

**Meaning:** Fewer than two distinct call sites agreed on a runtime base.

**Checks:**

1. Confirm the exact supported kernel.
2. Confirm the payload includes the no-upper-bound calibration fix.
3. Confirm `atrace sched --async_start` works from the ADB shell.
4. Do not substitute a hardcoded base from an older boot; KASLR changes across boots.

## `slide ktext anchor mismatch` or unexpected base

**Meaning:** Candidate sites disagree, or the image/table does not match the running kernel.

**Action:** Stop. Revalidate the input image hash and generated table. Do not proceed to direct writes.

## `pselect cannot place tree_prio`

**Meaning:** The direct stage used the older input-only encoder.

**Action:** Use the supplied vote5 payload or rebuild the included source. The direct path must use res encoding, with `pselect res attempt` in the log.

## `direct-r64-retry ... primitive-miss`

**Meaning:** The direct route did not complete its expected consumer/write sequence.

**Action:** Inspect the last synchronous markers in the newest `/data/local/tmp/sy_*.log`: `direct-w64`, `pselect res prepare`, `pselect res readiness`, and `pselect res attempt`. Do not repeatedly rerun without comparing those markers.

## Device reboots / `AP_S_PANIC`

**Meaning:** The kernel panicked during the attempt.

**Action:**

1. Wait for ADB to return and check `adb shell uptime`.
2. Preserve the newest device `sy_*.log` before another attempt.
3. Record which stage produced the last marker.
4. Do not start another runner while an old runner or exploit process is still active.

pstore may be inaccessible to the shell on production firmware. A missing pstore stack means the crash PC is unknown; do not infer it solely from a truncated stdout log.

## Ordinary shell still shows uid=2000

This is expected. Check root through the installed client:

```bash
adb -s "$HONOR_SERIAL" shell '/data/local/tmp/su -c "id"'
```

## No icon or app authorization dialog

This repository installs a temporary command-line su client/daemon, not a Magisk manager. Use ADB or create the visible proof file:

```bash
./scripts/write_root_proof.sh
```

Then open Files → Download → `honor-root-proof.txt`.

## Root disappears after reboot

This is the documented temporary-root boundary. No boot partition is modified. Run the device check and make a new deliberate attempt if root is needed again.

## Apps stop launching while temporary root is active

**Symptom:** after a successful run, tapping any icon on the launcher returns to the home screen immediately. Apps that were already running keep working.

**Observed evidence (AAP-AN00, kernel 6.12.38-android16, 2026-10-25):**

- `ActivityManager: Start proc <pid>:<pkg>` is followed within about 15 ms by `FATAL EXCEPTION: main` in that pid.
- Stack: `ClientTransactionListenerController.onContextConfigurationPreChanged` →
  `ConfigurationController.handleConfigurationChanged` → `ActivityThread.handleLaunchActivity` →
  `LaunchActivityItem.execute`. Exception is a `NullPointerException` on `Context.getResources()`.
- `ConfigurationController.handleConfigurationChanged` passes `ActivityThread.currentApplication()`, which
  returns `mInitialApplication`. A null argument means the process handled the launch transaction before
  its Application instance existed.
- The same tap opens the app normally after a reboot with no root active.
- The failing stage is `bindApplication`. On the app's binder thread:
  `Unable to open ashmem device: No such file or directory`, then
  `java.lang.IllegalArgumentException: FileDescriptor is not a valid ashmem fd` from
  `SharedMemory.<init>` → `SharedMemory$1.createFromParcel` → `Parcel.readTypedObject` →
  `IApplicationThread$Stub.onTransact`. Decompiling the device `framework.jar` shows
  `bindApplication` carries a `SharedMemory serializedSystemFontMap` argument. Unparcelling it throws,
  so `handleBindApplication` never runs and `mInitialApplication` stays null; the following launch
  transaction then hits the null context above. `am_proc_bound` is still logged on the system side.
- `/dev/ashmem` on this kernel does not implement the classic ashmem ioctls: `ASHMEM_SET_SIZE` and
  `ASHMEM_GET_SIZE` both fail with `ENOTTY` when run as shell or root. Ashmem is memfd-backed, and
  the validation path opens `/dev/ashmem` to compare device numbers, which matches the order of the
  two messages above.
- Reproduction needs an unlocked screen. With `mDreamingLockscreen=true`, `am start` reports the
  intent was delivered to the running top-most instance and no new app process is created.

The defect appears only for processes created after the exploit run. It reproduced with SELinux in both
`Permissive` and `Enforcing`.

**Checks performed, all negative:** display size and density overrides unchanged; `font_scale` 1.0;
CPU 772% idle with no runaway process; `/data` writable; launcher and already-running system processes
unaffected. All nine `policycap` flags, the SELinux domains of running apps, file and newly created
directory labels, `/dev` node count (340), `/dev/ashmem` permissions (666), the zygote `/dev` mount,
and the per-app mount namespaces are identical to a clean boot. Shell processes, `sh` children, root
processes, and provider processes created after the run can all open `/dev/ashmem`.

**Cause:** the exploit changes the `data` pointer of the boot_id entry in `random_table`.
It does not merely overwrite the UUID bytes.

Evidence chain:

- On a clean boot `/dev` contains a node whose name is literally `"/dev/ashmem" + boot_id`, for example
  `/dev/ashmem2ebadacf-cbac-4bfc-ad63-b31fbedfeb31` with `boot_id=2ebadacf-cbac-4bfc-ad63-b31fbedfeb31`.
  Its device number is `10,124`, the same as plain `/dev/ashmem`.
- Disassembling `/system/lib64/libcutils.so` shows the ashmem path being built as
  `std::string("/dev/ashmem") + Trim(ReadFileToString("/proc/sys/kernel/random/boot_id"))`, then opened
  with `__open_2(path, O_RDWR|O_CLOEXEC)`. When that open fails the library logs
  `Unable to open ashmem device: %m` and `SharedMemory` rejects the fd.
- The initialized table in the exact kernel image has `0xffffffc08250b558 = 0xffffffc082687770`;
  the latter is `sysctl_bootid`. The payload uses the table entry's `data` field as the direct-read
  oracle. After the reads, it points into per-CPU `entry_task` storage, so the formatted UUID varies
  with the task running when a process reads it. `evidence/app_bootid_probe.txt` records three
  different values for the shell-created node, the app-cached path, and a subsequent read.
- Processes that had already cached the path keep working. New processes can compute different
  paths, so creating a node for only the value observed by the shell is insufficient.

**Adopted fix:** after the final oracle read, the payload restores the `data` pointer to the runtime
address of `sysctl_bootid`. The shape=1 primitive also changes the first eight bytes at the source
address, so the resulting UUID is stable but differs from the original boot UUID.
`scripts/fix_ashmem_node.sh` then creates one node for that stable value. With the bootidfix payload,
Quark completed two consecutive cold starts; the second run had no new ashmem or FATAL records.
The payload is `prebuilt/preload_honor_vote6b_bootidfix.so`, SHA-256
`ecdcdfcfdc5ea9d5a56286b1f5367315d521d0fe738c03a63ceaa9cebaf658e2`.

**Restore attempts that failed (kept for reference in `evidence/experiments/`):**

- `boot_id_restore_attempt.patch` — restoring the UUID with the exploit's shape=1 kernel write
  panicked the device at the first restore write (`evidence/experiments/boot_id_restore_panic.txt`).
- `boot_id_procfs_restore_attempt.patch` — writing the UUID back through the procfs file. This
  kernel rejects write access to `/proc/sys/kernel/random/boot_id` even for root with full
  capabilities while SELinux is permissive (`dd: Permission denied`), so the write never lands.
  With this patch in the payload the device also panicked during the second oracle read in three
  consecutive runs; the panic is at the same pre-existing race point (`entry_task` read) and its
  cause is not determined. The patch was reverted and the source tree rebuilt back to the published
  vote6b hash.

**Diagnostic tools:** `scripts/probe_stop.c` freezes a newly created process, enters its mount
namespace, tests the device paths as root and as the app uid, and can read process memory.
`scripts/ashmem_probe.c` calls `ashmem_get_size_region` and `ashmem_create_region` directly, which
reproduces the libcutils behaviour without launching an app.

**Related observation:** the payload clears SELinux enforcing with a 64-bit write at
`selinux_state.enforcing`. In this kernel `initialized` is at offset 0x01 and `policycap[9]` at 0x02,
so the same write also touches the policycap bytes (layout in `evidence/selinux_state_layout.txt`).
Measured afterwards, the nine `policycap` flags match a clean boot exactly, so this does not explain
the launch failures.

**Recovery:** reboot the device. Root is lost with the reboot; the associated kernel-side state is gone too.

**Not a mitigation:** writing `1` to `/sys/fs/selinux/enforce` does not stop the crashes. It also makes the
su client unusable, because the shell domain cannot reach the daemon socket once enforcing is restored.
