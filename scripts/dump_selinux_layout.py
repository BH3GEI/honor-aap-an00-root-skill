#!/usr/bin/env python3
"""Dump selinux_state layout from the Honor Image and show which bytes an
8-byte write at the `enforcing` field offset would cover."""
import sys
from pathlib import Path

ROOT = Path("/Users/yao/Desktop/code/agent/honor-root-artifacts")
sys.path.insert(0, str(ROOT / "linuxoid"))

import generate_target as gt  # noqa: E402

boot_path = ROOT / "boot_a_175.img"
boot = gt.extract_boot_kernel(boot_path)
_, _config, _ = gt.extract_ikconfig(boot.kernel)
kallsyms = gt.recover_kallsyms(boot.kernel, boot.kernel_size, boot.image_size)
btf = gt.locate_btf(boot.kernel, kallsyms)

st = btf.struct("selinux_state")
print(f"selinux_state size = {st.size_type} (0x{st.size_type:x})")
print(f"{'byte':>6} {'bits':>6}  field")
rows = []
for m in st.members:
    off = m.bit_offset // 8
    rows.append((off, m))
    print(f"0x{off:04x} {m.bit_offset:5d}  {m.name}")

enfor = [off for off, m in rows if m.name == "enforcing"][0]
print()
print(f"enforcing byte offset = 0x{enfor:x}")
covered = [m.name for off, m in rows if enfor <= off < enfor + 8]
print(f"an 8-byte write at 0x{enfor:x} overwrites: {covered}")

# Which fields survive only if the write is narrowed to 1 byte.
print()
print("layout detail around the write:")
for off, m in rows:
    if -0 <= off - enfor < 32:
        rel = off - enfor
        mark = "  <-- write base" if rel == 0 else ("  <-- covered" if 0 < rel < 8 else "")
        print(f"  +{rel:<4d} {m.name}{mark}")
