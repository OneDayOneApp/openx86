#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""Difftest the Stage-A x86 core against Unicorn.

Assembles a flat test image, runs the Verilator model and Unicorn with the same
initial state, then compares GPRs, RIP, RFLAGS (modeled bits) and a memory hash.
"""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "tests")
TB = os.path.join(ROOT, "build", "obj_core", "core_tb")
BASE = 0x400000
HALT = 0xF0000000
FLAG_MASK = 0xFD5  # CF PF AF ZF SF TF IF DF OF


def assemble(src, name):
    os.makedirs(BUILD, exist_ok=True)
    obj = os.path.join(BUILD, name + ".o")
    binp = os.path.join(BUILD, name + ".bin")
    subprocess.run(["clang", "-target", "x86_64-unknown-none-elf", "-c", src, "-o", obj],
                   check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", obj, binp], check=True)
    with open(binp, "rb") as f:
        return f.read(), binp


def run_rtl(binp):
    dump = binp + ".mem"
    env = dict(os.environ)
    env["DUMPMEM"] = dump
    out = subprocess.run([TB, binp], capture_output=True, text=True, timeout=600, env=env)
    if out.returncode not in (0, 3):
        print(out.stdout)
        print(out.stderr)
        raise SystemExit(f"RTL simulation failed (rc={out.returncode})")
    line = out.stdout.strip().splitlines()[-1]
    res = json.loads(line)
    res["memdump"] = open(dump, "rb").read() if os.path.exists(dump) else b""
    return res


def memhash(data):
    h = 1469598103934665603
    for start, size in ((0x7F0000, 0x10000), (0x600000, 0x10000)):
        for b in data[start:start + size]:
            h ^= b
            h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def run_unicorn(image):
    sys.path.insert(0, os.path.join(ROOT, ".home", "pylibs"))
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (
        UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX,
        UC_X86_REG_RSP, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
        UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11,
        UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15,
        UC_X86_REG_RIP, UC_X86_REG_EFLAGS,
        UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3,
        UC_X86_REG_XMM4, UC_X86_REG_XMM5, UC_X86_REG_XMM6, UC_X86_REG_XMM7,
        UC_X86_REG_XMM8, UC_X86_REG_XMM9, UC_X86_REG_XMM10, UC_X86_REG_XMM11,
        UC_X86_REG_XMM12, UC_X86_REG_XMM13, UC_X86_REG_XMM14, UC_X86_REG_XMM15,
    )

    mu = Uc(UC_ARCH_X86, UC_MODE_64)
    mu.mem_map(0, 16 * 1024 * 1024)
    mu.mem_map(HALT & ~0xFFF, 0x1000)
    mu.mem_write(BASE, image)
    mu.reg_write(UC_X86_REG_RIP, BASE)
    mu.reg_write(UC_X86_REG_EFLAGS, 0x202)

    stop = {"code": None}

    def hook(uc, access, addr, size, value, user_data):
        if addr == HALT:
            stop["code"] = value & 0xFFFFFFFF
            uc.emu_stop()

    mu.hook_add(UC_HOOK_MEM_WRITE, hook)
    try:
        mu.emu_start(BASE, 0, 0, 20000000)
    except Exception as e:  # noqa: BLE001
        raise SystemExit(f"unicorn stopped with error: {e}")

    regs = [UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX,
            UC_X86_REG_RSP, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
            UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11,
            UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15]
    gpr = [mu.reg_read(r) for r in regs]
    xmms = [UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3,
            UC_X86_REG_XMM4, UC_X86_REG_XMM5, UC_X86_REG_XMM6, UC_X86_REG_XMM7,
            UC_X86_REG_XMM8, UC_X86_REG_XMM9, UC_X86_REG_XMM10, UC_X86_REG_XMM11,
            UC_X86_REG_XMM12, UC_X86_REG_XMM13, UC_X86_REG_XMM14, UC_X86_REG_XMM15]
    xmm = [mu.reg_read(r) for r in xmms]
    rip = mu.reg_read(UC_X86_REG_RIP)
    flags = mu.reg_read(UC_X86_REG_EFLAGS)
    data = bytes(mu.mem_read(0, 16 * 1024 * 1024))
    return {"gpr": gpr, "xmm": xmm, "rip": rip, "flags": flags, "memhash": memhash(data),
            "exit": stop["code"], "memdump": data[0x7F0000:0x800000] + data[0x600000:0x610000]}


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: difftest.py <test.S>")
    src = sys.argv[1]
    name = os.path.splitext(os.path.basename(src))[0]
    image, binp = assemble(src, name)

    rtl = run_rtl(binp)
    ref = run_unicorn(image)

    errors = []
    rtl_gpr = [int(x, 16) for x in rtl["gpr"]]
    for i, (a, b) in enumerate(zip(rtl_gpr, ref["gpr"])):
        if a != b:
            errors.append(f"gpr[{i}] rtl=0x{a:016x} ref=0x{b:016x}")
    rtl_xmm = [int(x, 16) for x in rtl.get("xmm", [])]
    for i, (a, b) in enumerate(zip(rtl_xmm, ref["xmm"])):
        if a != b:
            errors.append(f"xmm[{i}] rtl=0x{a:032x} ref=0x{b:032x}")
    rtl_rip = int(rtl["rip"], 16)
    # Unicorn's MEM_WRITE hook fires before RIP advances past the halting store,
    # so allow the RTL RIP to be within the size of that final instruction.
    if rtl_rip != ref["rip"] and not (ref["rip"] < rtl_rip <= ref["rip"] + 15):
        errors.append(f"rip rtl={rtl['rip']} ref=0x{ref['rip']:016x}")
    rtl_flags = int(rtl["flags"], 16)
    if (rtl_flags & FLAG_MASK) != (ref["flags"] & FLAG_MASK):
        errors.append(f"flags rtl=0x{rtl_flags:016x} ref=0x{ref['flags']:016x}")
    if int(rtl["memhash"], 16) != ref["memhash"]:
        errors.append(f"memhash rtl={rtl['memhash']} ref=0x{ref['memhash']:016x}")

    if errors:
        print(f"FAIL {name}")
        for e in errors[:20]:
            print("  " + e)
        print(f"  rtl cycles={rtl['cycles']} halted={rtl['halted']} fatal={rtl['fatal']} "
              f"fatalPc={rtl['fatalPc']} code={rtl['fatalCode']} exit={rtl['exit']}")
        a, b = rtl["memdump"], ref["memdump"]
        shown = 0
        for off in range(min(len(a), len(b))):
            if a[off] != b[off]:
                region = "stack" if off < 0x10000 else "data"
                base = 0x7F0000 if off < 0x10000 else 0x600000
                lo = off % 0x10000
                print(f"  mem diff {region}+0x{lo:x} (0x{base+lo:x}): rtl=0x{a[off]:02x} ref=0x{b[off]:02x}")
                shown += 1
                if shown >= 10:
                    break
        sys.exit(1)

    print(f"PASS {name}: {rtl['cycles']} cycles, exit={rtl['exit']}, "
          f"rip={rtl['rip']}, ref_rip=0x{ref['rip']:016x}")


if __name__ == "__main__":
    main()
