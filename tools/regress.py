#!/usr/bin/env python3
"""Run every assembly test in tests/asm through the difftest harness."""

import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = sorted(glob.glob(os.path.join(ROOT, "tests", "asm", "*.S")))


def main():
    only = sys.argv[1:] if len(sys.argv) > 1 else None
    passed, failed = 0, []
    for src in TESTS:
        name = os.path.splitext(os.path.basename(src))[0]
        if only and name not in only:
            continue
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "difftest.py"), src],
                           capture_output=True, text=True)
        if r.returncode == 0:
            passed += 1
            print(r.stdout.strip().splitlines()[-1])
        else:
            failed.append(name)
            print(r.stdout.strip())
            print(r.stderr.strip())
    print(f"\nregress: {passed} passed, {len(failed)} failed")
    if failed:
        print("failed: " + ", ".join(failed))
        sys.exit(1)


if __name__ == "__main__":
    main()
