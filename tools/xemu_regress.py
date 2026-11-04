#!/usr/bin/env python3
"""Run Stage-A assembly tests against the self-written XEMU reference model."""

import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import difftest  # noqa: E402

TB = os.path.join(ROOT, "build", "obj_core", "core_tb")
REF = os.path.join(ROOT, "build", "ref", "libxemu.so")
TESTS = sorted(glob.glob(os.path.join(ROOT, "tests", "asm", "*.S")))


def main():
    only = sys.argv[1:] if len(sys.argv) > 1 else None
    passed, failed = 0, []
    for src in TESTS:
        name = os.path.splitext(os.path.basename(src))[0]
        if only and name not in only:
            continue
        _, binp = difftest.assemble(src, name)
        r = subprocess.run([TB, "--diff", f"--ref-so={REF}", "-i", binp],
                           capture_output=True, text=True, timeout=600)
        ok = r.returncode == 0 and "instructions cross-checked" in r.stderr
        if ok:
            passed += 1
            n = [l for l in r.stderr.splitlines() if "instructions cross-checked" in l][-1]
            print(f"PASS {name}: {n.split()[1]} instructions")
        else:
            failed.append(name)
            print(f"FAIL {name}")
            print("\n".join(r.stderr.strip().splitlines()[-3:]))
    print(f"\nxemu regress: {passed} passed, {len(failed)} failed")
    if failed:
        print("failed: " + ", ".join(failed))
        sys.exit(1)


if __name__ == "__main__":
    main()
