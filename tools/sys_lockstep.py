#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""Per-instruction lockstep difftest for system tests against XEMU."""

import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TB = os.path.join(ROOT, "build", "obj_core", "core_tb")
REF = os.path.join(ROOT, "build", "ref", "libxemu.so")
BUILD = os.path.join(ROOT, "build", "sys")


def assemble(src, name):
    os.makedirs(BUILD, exist_ok=True)
    obj = os.path.join(BUILD, name + ".o")
    binp = os.path.join(BUILD, name + ".bin")
    subprocess.run(["clang", "-target", "x86_64-unknown-none-elf", "-c", src, "-o", obj], check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", obj, binp], check=True)
    return binp


# Tests that the RTL implements today. Newer XEMU-only system tests (CPL3,
# TSS, timer IRQ) are exercised by tools/xemu_sys.py instead.
RTL_TESTS = [
    "t22_cr", "t23_paging", "t25_trap", "t26_pf_trap", "t27_iret",
    "t28_syscall", "t29_cpuid", "t30_invlpg", "t31_fetchpf", "t32_intn",
]


def main():
    tests = [os.path.join(ROOT, "tests", "system", n + ".S") for n in RTL_TESTS]
    passed, failed = 0, []
    for src in tests:
        name = os.path.splitext(os.path.basename(src))[0]
        binp = assemble(src, name)
        r = subprocess.run([TB, binp, "2000000", "--diff", f"--ref-so={REF}"],
                           capture_output=True, text=True, timeout=600)
        if r.returncode == 0 and "instructions cross-checked" in r.stderr:
            passed += 1
            n = [l for l in r.stderr.splitlines() if "instructions cross-checked" in l][-1]
            print(f"PASS {name}: {n.split()[1]} instructions")
        else:
            failed.append(name)
            print(f"FAIL {name}")
            print("\n".join(r.stderr.strip().splitlines()[-3:]))
    print(f"\nsys lockstep: {passed} passed, {len(failed)} failed")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
