#!/usr/bin/env python3
"""Patch a Magisk ARM64 binary to use /data/adb/m as MAGISKTMP."""
import argparse
from pathlib import Path

OLD = b"/debug_ramdisk"
NEW_PREFIX = b"/data/adb/m"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = args.input.read_bytes()
    count = data.count(OLD)
    if count == 0:
        raise SystemExit("no /debug_ramdisk string found; wrong binary or already patched")
    replacement = NEW_PREFIX + b"\x00" * (len(OLD) - len(NEW_PREFIX))
    patched = data.replace(OLD, replacement)
    assert len(patched) == len(data)
    args.output.write_bytes(patched)
    print(f"patched={args.output} replacements={count}")


if __name__ == "__main__":
    main()
