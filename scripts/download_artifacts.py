#!/usr/bin/env python3
"""Download and verify every public reproduction artifact.

Only Python's standard library is required. Existing files are accepted only
when their SHA-256 matches the pinned value.
"""
import argparse
import hashlib
import os
from pathlib import Path
import urllib.request

BASE = "https://github.com/BH3GEI/honor-aap-an00-root-skill/releases/download"
ARTIFACTS = [
    ("firmware-v175", "boot_a_175.img", "firmware/boot_a_175.img",
     "685ebbe298fb47321c6db6336adc2cb74da0b8ecca088242626544b2a3b0813d"),
    ("firmware-v175", "Image_175", "firmware/Image_175",
     "09447d275e3f66d9bd70f68893757464e416d82dc4cbb16c544a3a7b9023cfd0"),
    ("firmware-v175", "init_boot_a_175.img", "firmware/init_boot_a_175.img",
     "c8df2e291ce6dc0067bca47ae199c83bc6ec82ddaccf670099eb9a6bf1860929"),
    ("firmware-v175", "vendor_boot_a_175.img", "firmware/vendor_boot_a_175.img",
     "869341c84e64243a846a2c963affd1b576ec5a8f81b5e29de3e6aa1c8d3581c3"),
    ("firmware-v175", "dtbo_a_175.img", "firmware/dtbo_a_175.img",
     "d9046c74b78111f7047cfe1e6d74fcb204853e181116a58f76470a296979e639"),
    ("firmware-v175", "vbmeta_a_175.img", "firmware/vbmeta_a_175.img",
     "b855b9c4f53faec706f86ce04d00f11de7b6fdbe2a43e8fbc902e05312a935ca"),
    ("payload-vote6b", "preload_honor_vote6b.so", "prebuilt/preload_honor_vote6b.so",
     "e7e2a288e56978f59b7680053f2f5e657b461498bc2563c45ff4322c0977217b"),
    ("magisk-runtime-v1", "Magisk-refresh.apk", "magisk-runtime/Magisk-refresh.apk",
     "fec1bf3c2d8c1ab04511b6128af1738cc9e7f371fab5c4a1e70406f1459914be"),
    ("magisk-runtime-v1", "magisk", "magisk-runtime/magisk",
     "028081b5b465d01a2a65e5b3e4d15d53b60b37f4336bd55fea1a3b0e50a116a7"),
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(release: str, name: str, destination: Path, expected: str) -> None:
    if destination.is_file() and sha256(destination) == expected:
        print(f"ok {destination} (already verified)")
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    url = f"{BASE}/{release}/{name}"
    temporary = destination.with_suffix(destination.suffix + ".part")
    digest = hashlib.sha256()
    try:
        print(f"download {url}")
        with urllib.request.urlopen(url, timeout=90) as response, temporary.open("wb") as out:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                out.write(chunk)
                digest.update(chunk)
        actual = digest.hexdigest()
        if actual != expected:
            raise RuntimeError(f"hash mismatch for {name}: {actual} != {expected}")
        os.replace(temporary, destination)
        print(f"ok {destination}")
    finally:
        if temporary.exists():
            temporary.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", choices=["firmware", "magisk"], default=None)
    args = parser.parse_args()
    for release, name, relative, expected in ARTIFACTS:
        if args.only == "firmware" and not relative.startswith("firmware/"):
            continue
        if args.only == "magisk" and not relative.startswith("magisk-runtime/"):
            continue
        download(release, name, Path(relative), expected)
    print("all artifacts verified")


if __name__ == "__main__":
    main()
