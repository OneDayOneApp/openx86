#!/usr/bin/env python3
"""Run all Stage-B OoO tests against the Stage-A reference core."""

import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = sorted(glob.glob(os.path.join(ROOT, "tests", "ooo", "*.S")))


def main():
    ok, bad = 0, []
    for src in TESTS:
        name = os.path.splitext(os.path.basename(src))[0]
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "ooocmp.py"), src],
                           capture_output=True, text=True)
        if r.returncode == 0:
            ok += 1
            print(r.stdout.strip())
        else:
            bad.append(name)
            print(r.stdout.strip())
    print(f"\nooo regress: {ok} passed, {len(bad)} failed")
    if bad:
        print("failed: " + ", ".join(bad))
        sys.exit(1)


if __name__ == "__main__":
    main()
