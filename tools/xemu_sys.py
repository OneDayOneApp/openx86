#!/usr/bin/env python3
"""Run XEMU-only system tests (CPL3/TSS/timer IRQ) on the standalone runner.

These features are not implemented in the RTL, so the oracle is the expected
exit code written to the halt MMIO by each test.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XEMU = os.path.join(ROOT, "build", "xemu")
BUILD = os.path.join(ROOT, "build", "sys")

# test -> expected halt exit code
CASES = [
    # P3: system mode
    ("t40_cpl3", 0x7a),
    ("t41_timer", 0x07),
    # L0: port I/O + devices
    ("t50_uart", 0x50),
    ("t51_pit", 0x60),
    ("t52_portio", 0x70),
]


def assemble(name):
    src = os.path.join(ROOT, "tests", "system", name + ".S")
    obj = os.path.join(BUILD, name + ".o")
    binp = os.path.join(BUILD, name + ".bin")
    os.makedirs(BUILD, exist_ok=True)
    subprocess.run(["clang", "-target", "x86_64-unknown-none-elf", "-c", src, "-o", obj],
                   check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", obj, binp], check=True)
    return binp


def main():
    passed, failed = 0, []
    for name, want in CASES:
        binp = assemble(name)
        r = subprocess.run([XEMU, "-b", "-i", binp], capture_output=True, text=True,
                           timeout=300)
        out = r.stdout + r.stderr
        got = r.returncode
        if got == want:
            passed += 1
            line = [l for l in out.splitlines() if l.startswith("xemu: halted")]
            print(f"PASS {name}: exit=0x{got:02x} ({line[-1] if line else ''})")
        else:
            failed.append(name)
            print(f"FAIL {name}: exit=0x{got:02x} want 0x{want:02x}")
            for l in out.strip().splitlines()[-4:]:
                print("  " + l)
    print(f"\nxemu sys: {passed} passed, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
