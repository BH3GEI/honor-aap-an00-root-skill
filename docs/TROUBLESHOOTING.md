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
