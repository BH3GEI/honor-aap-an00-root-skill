# AGENTS.md

Cross-harness entry point for agents working on Honor 500 AAP-AN00 temporary root.

## When this applies

Use this repository for CVE-2026-43499 work, temporary root reproduction, KASLR calibration, res-encoded pselect waiters, or troubleshooting on the Honor AAP-AN00 / HNAAP with the exact supported firmware.

## Required reading

1. [`honor-aap-an00-root/SKILL.md`](honor-aap-an00-root/SKILL.md) — canonical build, run, and verification workflow.
2. [`honor-aap-an00-root/reference.md`](honor-aap-an00-root/reference.md) — exploit geometry, KASLR voting, direct-stage details, and failure interpretation.

## Safety invariants

1. Use only on a device you own or are explicitly authorized to test.
2. Run only on `AAP-AN00`, display `AAP-AN00 10.0.0.175(C00E170R104P3)`, and kernel `6.12.38-android16-5-gfde7767f6ef6-abogki481467632-4k`.
3. Never flash boot, init_boot, vbmeta, or other partitions for this workflow.
4. Do not start concurrent exploit runners. One attempt may panic and reboot the device.
5. Do not automatically loop exploit attempts. Read `runs/` and the newest device `sy_*.log` first.
6. Do not copy upstream challenge-signing key material into this repository.
7. Treat root as temporary. Do not claim Magisk persistence, app-level root management, or post-reboot persistence.

## Commands

```bash
./scripts/check_env.sh -s <serial> --require-target
./scripts/run_root.sh -s <serial>              # verified prebuilt
./scripts/run_root.sh -s <serial> --build      # rebuild from source
./scripts/verify_root.sh -s <serial>
./scripts/start_magisk.sh -s <serial>       # optional runtime Magisk
./scripts/write_root_proof.sh -s <serial>      # optional visible proof
```

`honor-aap-an00-root/` is the canonical skill directory. After editing its docs, run `scripts/sync_mirrors.sh` to refresh the Claude and Cursor mirrors.
