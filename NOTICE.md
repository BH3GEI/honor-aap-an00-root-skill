# Attribution and upstream materials

## Reference workflow

Repository structure and agent-skill packaging follow:

- https://github.com/BH3GEI/redmagic-nx809j-root-skill

The RedMagic EDL/GBL/Magisk workflow is not used on this Honor target.

## Payload base

Modified from:

- Linuxoid-cn/CVE-2026-43499-Poc-Analysis
- https://github.com/Linuxoid-cn/CVE-2026-43499-Poc-Analysis
- Upstream commit used by the working tree: `d7e805fc1a13b87ecf644a8869e6d313f2d1ecde`

The upstream license is preserved in `LICENSE`. This repository adds the Honor target, res-encoded slide/direct pselect paths, KASLR call-site voting, runtime image-address handling, synchronous diagnostics, and reproduction tooling.

`scripts/fetch_upstream.sh` clones that pinned upstream commit into the ignored `vendor/linuxoid/` directory. The upstream challenge-signing material is used locally by `scripts/run_exploit.py`; it is intentionally not duplicated in this repository.

## Research references

The archived project notes also examined the NebuSec / CyberMeowfia CVE-2026-43499 tree and public case matrices. The successful payload in this repository was built and tested from the included modified Linuxoid source; no private CyberMeowfia payload is included or required.

## Magisk runtime package

Magisk v30.7 is used under its upstream license:

- https://github.com/topjohnwu/Magisk

`patches/magisk-refresh.patch` changes state refresh and menu initialization in the Android app. `scripts/patch_magisk_binary.py` changes MAGISKTMP to `/data/adb/m` for this runtime-only setup. The custom APK and daemon are distributed through the `magisk-runtime-v1` Release. They are not part of the upstream Magisk release.

## Device evidence

The successful execution and independent verification were performed on an Honor AAP-AN00 running the exact firmware listed in `README.md`. Logs are included under `evidence/` with ANSI control sequences retained only where they came from the original payload output.
