#!/usr/bin/env python3
"""Populate Magisk native/out from the official Magisk APK.

The Android application build packages these files instead of rebuilding the
native project. Use --patched-magisk to substitute the ARM64 daemon/client.
"""
import argparse
import zipfile
from pathlib import Path

ABIS = ["armeabi-v7a", "arm64-v8a", "x86", "x86_64"]
FILES = {
    "magiskboot": "libmagiskboot.so",
    "magiskinit": "libmagiskinit.so",
    "magiskpolicy": "libmagiskpolicy.so",
    "magisk": "libmagisk.so",
    "libinit-ld.so": "libinit-ld.so",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True,
                        help="Magisk source checkout root")
    parser.add_argument("--patched-magisk", type=Path,
                        help="optional patched ARM64 libmagisk.so")
    args = parser.parse_args()
    with zipfile.ZipFile(args.apk) as apk:
        for abi in ABIS:
            out = args.source / "native/out" / abi
            out.mkdir(parents=True, exist_ok=True)
            for destination, source in FILES.items():
                data = apk.read(f"lib/{abi}/{source}")
                if abi == "arm64-v8a" and destination == "magisk" and args.patched_magisk:
                    data = args.patched_magisk.read_bytes()
                (out / destination).write_bytes(data)
            print(f"prepared {out}")


if __name__ == "__main__":
    main()
