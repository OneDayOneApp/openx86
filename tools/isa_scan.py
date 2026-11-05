#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""Find instructions in a kernel image that XEMU's decoder rejects.

Disassembles the executable sections with llvm-objdump, then feeds every
instruction's raw bytes to the real XEMU decoder (xemu/isa_scan.c). Reports
one `ADDR BYTES MNEMONIC` line per unsupported instruction plus a histogram
of the mnemonics, so the missing opcodes can be implemented in one pass
instead of one boot crash at a time.
"""

import argparse
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBJDUMP = "/opt/homebrew/opt/llvm/bin/llvm-objdump"

LINE_RE = re.compile(
    r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*(.*)$")


def disassemble(path):
    out = subprocess.run([OBJDUMP, "-d", "--no-show-raw-insn", path],
                         capture_output=True, text=True, check=False)
    text = subprocess.run([OBJDUMP, "-d", path],
                          capture_output=True, text=True, check=False)
    if text.returncode != 0:
        raise SystemExit(f"llvm-objdump failed: {text.stderr[:400]}")
    del out
    insns = []
    for line in text.stdout.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        raw = "".join(m.group(2).split())
        mnem = m.group(3).strip()
        # Skip the pseudo "..." continuation lines objdump emits.
        if not raw or "..." in mnem:
            continue
        insns.append((addr, raw, mnem))
    return insns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--scan-bin", default=os.path.join(ROOT, "build", "xemu_isa_scan"))
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args()

    insns = disassemble(args.image)
    tmp = os.path.join(ROOT, "build", "isa_scan_input.txt")
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    with open(tmp, "w") as f:
        for addr, raw, mnem in insns:
            f.write(f"{addr:x} {raw} {mnem}\n")

    r = subprocess.run([args.scan_bin, tmp], capture_output=True, text=True)
    bad = r.stdout.splitlines()
    sys.stderr.write(r.stderr)

    hist = collections.Counter()
    for line in bad:
        parts = line.split(None, 2)
        if len(parts) == 3:
            mnem = parts[2].split()[0] if parts[2].split() else "?"
            hist[mnem] += 1

    print(f"# unsupported: {len(bad)} / {len(insns)} instructions")
    for mnem, n in hist.most_common(args.top):
        print(f"# {n:6d}  {mnem}")
    for line in bad:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
