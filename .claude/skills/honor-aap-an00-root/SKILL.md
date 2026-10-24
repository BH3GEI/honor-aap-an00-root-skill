---
name: honor-aap-an00-root
description: Reproduce temporary root on Honor 500 AAP-AN00 / HNAAP (SM8850, Android 16, 6.12.38 GKI) with a modified CVE-2026-43499 payload, res-encoded pselect waiters, and tracefs KASLR voting. Use for exact-firmware root reproduction, build/run/verify tooling, or analysis of the Honor inline futex adaptation.
---

# Honor 500 AAP-AN00 Temporary Root Skill

## Scope and verified result

This workflow obtains temporary root on the exact supported Honor firmware. It does not unlock the bootloader, patch a boot image, or install persistent Magisk.

A successful run installs an embedded su client and daemon, produces `uid=0(root)` with `u:r:kernel:s0`, and leaves SELinux permissive. Independent ADB verification is included in `evidence/`.

## Required target

| Check | Required value |
|---|---|
| Model | `AAP-AN00` |
| Device | `HNAAP` |
| Display | `AAP-AN00 10.0.0.175(C00E170R104P3)` |
| Kernel | `6.12.38-android16-5-gfde7767f6ef6-abogki481467632-4k` |
| Image SHA-256 | `09447d275e3f66d9bd70f68893757464e416d82dc4cbb16c544a3a7b9023cfd0` |

`scripts/check_env.sh --require-target` enforces the model, display, and kernel checks before the exploit runner starts.

## Safety rules

1. Back up important data. An exploit attempt can panic and reboot the device.
2. Use one ADB target explicitly with `HONOR_SERIAL` or `--serial`.
3. Do not run multiple exploit drivers concurrently.
4. Do not automatically repeat failed attempts. Inspect logs and device state first.
5. Do not flash or wipe any partition.
6. Do not run the supplied payload on a different kernel build.
7. Keep the upstream signing material in the ignored vendor checkout; do not commit it.

## Artifacts

| Path | Purpose |
|---|---|
| `prebuilt/preload_honor_vote5.so` | Exact payload used in the successful run |
| `prebuilt/SHA256SUMS.txt` | Prebuilt integrity check |
| `payload/` | Complete modified source and build system |
| `scripts/run_root.sh` | Device check, push, one attempt, verification |
| `scripts/run_exploit.py` | Challenge response and ADB-dark protocol |
| `scripts/verify_root.sh` | Independent `su -c id` and SELinux verification |
| `evidence/` | Successful execution and independent proof |

## Workflow A: verified prebuilt payload

### 1. Prepare the host

Install `adb`, `python3`, and `git`. On macOS:

```bash
brew install android-platform-tools python git
```

Enable USB debugging, connect the phone, and authorize the host.

### 2. Select and validate the device

```bash
export HONOR_SERIAL=<adb-serial>
./scripts/check_env.sh --require-target
```

Required output includes `exact supported firmware`. If the device already has an active temporary-root session, `run_root.sh` reports it and does not rerun the exploit.

### 3. Fetch pinned upstream signing material

```bash
./scripts/fetch_upstream.sh
```

This clones the upstream project at commit `d7e805fc1a13b87ecf644a8869e6d313f2d1ecde` into `vendor/linuxoid/`. The runner reads `vendor/linuxoid/private_key.json` locally and never prints it.

`run_root.sh` performs this step automatically when needed.

### 4. Execute one attempt

```bash
./scripts/run_root.sh
```

The script verifies this prebuilt SHA-256 before pushing:

```text
d97f486356914a10f3b7059fea9c39ed85e53fb295098da85aa414003d32b741
```

The payload is pushed as `/data/local/tmp/preload_honor.so`. The driver polls only until the challenge is answered, then stops ADB traffic for the exploit window. It saves the full log under `runs/`.

Do not issue ADB commands while the dark window is active unless the device has already rebooted or the runner has exited.

### 5. Verify independently

```bash
./scripts/verify_root.sh
```

Expected root identity:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

Expected SELinux result:

```text
Permissive
```

The ordinary shell remains `uid=2000(shell)`. This is expected; use the installed su client for root operations.

### 6. Use root

Interactive ADB root shell:

```bash
adb -s "$HONOR_SERIAL" shell
/data/local/tmp/su
```

Single command:

```bash
adb -s "$HONOR_SERIAL" shell '/data/local/tmp/su -c "id"'
```

Optional proof visible in the phone's file manager:

```bash
./scripts/write_root_proof.sh
```

Open **Files → Download → honor-root-proof.txt**.

## Workflow B: rebuild from source

An Android NDK is required only for this workflow. NDK r29 was used for the verified build.

```bash
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r29
./scripts/build.sh
./scripts/run_root.sh --build
```

`build.sh` runs `make -C payload preload` and produces `payload/build/bin/preload.so`. The runner uses this binary instead of the preserved vote5 artifact.

The source tree already contains the generated target offsets and KASLR site table. The original kernel image is not required for an exact-firmware rebuild.

## Interpreting the result

- `slide-kaslr-ok`: slide confirmed the runtime kernel-text base through boot_id.
- `direct-r64-oracle ... ok=1`: the direct read primitive returned the expected sidecar values.
- `direct credential result uid=0`: init_cred installation succeeded.
- `direct-root-summary root=1`: root, id, su installation, and SELinux checks passed in the payload.
- `RESULT: ROOT OK`: the host runner found the success summary.
- `verify_root.sh`: independent proof from a new ADB command; this is the final acceptance check.

## After reboot

Temporary root and its daemon are gone after a reboot. The ordinary Android system remains intact. Return to the device check and run one new attempt only if needed:

```bash
./scripts/check_env.sh --require-target
./scripts/run_root.sh
```

## Reference

Read `reference.md` for the inline-futex capacity problem, res encoding, KASLR voting, direct-stage address domains, failure diagnosis, and regeneration procedure.
