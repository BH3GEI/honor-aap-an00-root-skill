# Honor 500 (AAP-AN00) Temporary Root Skill

A reproducible, source-available implementation of **temporary userspace root** on the Honor 500 **AAP-AN00** using a modified Linuxoid CVE-2026-43499 payload. It adapts the exploit to the inline futex call chain in the device's 6.12.38 GKI kernel and includes the successful ARM64 binary, build system, runner, verification scripts, and on-device evidence.

**Verified result:** `uid=0(root)`, `u:r:kernel:s0`, SELinux `Permissive`, and a working `su` client/daemon. No boot image is patched or flashed. Root does not persist across reboot.

## Supported target

| Property | Required value |
|---|---|
| Model | `AAP-AN00` |
| Device | `HNAAP` |
| SoC | Qualcomm SM8850 |
| Display build | `AAP-AN00 10.0.0.175(C00E170R104P3)` |
| Kernel release | `6.12.38-android16-5-gfde7767f6ef6-abogki481467632-4k` |
| Kernel Image SHA-256 | `09447d275e3f66d9bd70f68893757464e416d82dc4cbb16c544a3a7b9023cfd0` |

The runner refuses a different model, display build, or kernel by default. The supplied offsets and call-site table are firmware-specific.

## Verified proof

The included `evidence/` directory contains the complete successful run and an independent ADB verification:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

See [SUCCESS.zh-CN.md](docs/SUCCESS.zh-CN.md) and [root_verification.txt](evidence/root_verification.txt).

## Quick reproduction

### 1. Requirements

- macOS or Linux host with USB access
- `adb`, `python3`, `git`
- An Android NDK only when rebuilding; the prebuilt payload needs no NDK
- USB debugging enabled on the exact supported device
- A data backup and acceptance that a kernel exploit attempt can panic and reboot the phone

Install host packages on macOS with Homebrew:

```bash
brew install android-platform-tools python git
```

### 2. Clone and inspect the device

```bash
git clone https://github.com/BH3GEI/honor-aap-an00-root-skill.git
cd honor-aap-an00-root-skill
export HONOR_SERIAL=<your-adb-serial>
./scripts/check_env.sh
```

The output must say `exact supported firmware` before continuing.

### 3. Run once with the verified prebuilt payload

```bash
./scripts/run_root.sh
```

`run_root.sh` performs the following actions:

1. Refuses unsupported firmware and detects an already-active root session.
2. Fetches the pinned upstream package used for challenge signing.
3. Verifies the prebuilt SHA-256 and pushes it to `/data/local/tmp/preload_honor.so`.
4. Runs one exploit attempt with an ADB-silent window.
5. Independently verifies root when the payload reports success.

A failed attempt can leave the phone rebooting. Do not start a second runner while another is active.

### 4. Verify and use root

```bash
./scripts/verify_root.sh
./scripts/write_root_proof.sh   # optional: Files > Download > honor-root-proof.txt

adb -s "$HONOR_SERIAL" shell
/data/local/tmp/su
# Expected prompt: HNAAP:/ #
```

For one command:

```bash
adb -s "$HONOR_SERIAL" shell '/data/local/tmp/su -c "id"'
```

The ordinary `adb shell` remains `uid=2000`; invoke `/data/local/tmp/su` for root. This repository does not install a Magisk manager or an app-level authorization UI.

## Rebuild from source

Set an NDK r29 installation and rebuild instead of using the prebuilt binary:

```bash
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r29
./scripts/build.sh
./scripts/run_root.sh --build
```

The build produces `payload/build/bin/preload.so`. It uses the generated `src/target.h`, `src/ktext_sites.h`, and the included embedded su daemon. The successful vote5 binary is preserved separately in `prebuilt/`.

## Repository layout

```text
honor-aap-an00-root-skill/
├── AGENTS.md
├── honor-aap-an00-root/
│   ├── SKILL.md                 # canonical agent workflow
│   └── reference.md             # exploit architecture and troubleshooting
├── payload/                     # modified Linuxoid source and Makefile
├── prebuilt/                    # verified vote5 payload + SHA-256
├── scripts/                     # check/build/run/verify utilities
├── tools/                       # offset generation and diagnostics
├── evidence/                    # successful run and independent verification
├── docs/                        # Chinese reproduction, firmware, proof
├── .claude/skills/              # Claude Code mirror
└── .cursor/skills/              # Cursor mirror
```

## Important limits

Use only on a device you own or are explicitly authorized to test.

- Temporary runtime root only; no bootloader unlock and no persistent Magisk installation.
- The exploit changes credentials and SELinux state, writes `/data/local/tmp/su`, `/apex/com.android.virt/bin/su`, and starts a temporary su daemon.
- A failed attempt may cause `AP_S_PANIC` and an automatic reboot.
- The ordinary root shell is intended for ADB use; third-party apps do not automatically gain root access.
- Do not run this payload on another Honor model or another kernel build without regenerating and validating all offsets.

## Documentation

- [Canonical skill](honor-aap-an00-root/SKILL.md)
- [Technical reference](honor-aap-an00-root/reference.md)
- [中文完整复现指南](docs/REPRODUCE.zh-CN.md)
- [Firmware and hashes](docs/FIRMWARE.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Upstream and attribution](NOTICE.md)

## License

GNU AGPL v3. The payload derives from the Linuxoid project; see `LICENSE` and `NOTICE.md`.
