#!/usr/bin/env python3
"""System-mode co-simulation: run the DUT and XEMU on the same image and
compare the final architectural state."""

import ctypes
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import sysrun  # noqa: E402

LIB = os.path.join(ROOT, "build", "ref", "libxemu.so")
TO_REF, TO_DUT = 1, 0


class DiffState(ctypes.Structure):
    _fields_ = [("gpr", ctypes.c_uint64 * 16),
                ("rip", ctypes.c_uint64),
                ("rflags", ctypes.c_uint64),
                ("xmm", (ctypes.c_uint64 * 2) * 16)]


def run_xemu(image_path, max_steps=2_000_000):
    lib = ctypes.CDLL(LIB)
    lib.difftest_init.argtypes = [ctypes.c_int]
    lib.difftest_memcpy.argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    lib.difftest_regcpy.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.difftest_exec.argtypes = [ctypes.c_uint64]
    lib.difftest_halted.restype = ctypes.c_int
    data = open(image_path, "rb").read()
    lib.difftest_init(0)
    buf = ctypes.create_string_buffer(data, len(data))
    lib.difftest_memcpy(0x400000, buf, len(data), TO_REF)
    st = DiffState()
    st.rip = 0x400000
    st.rflags = 0x202
    lib.difftest_regcpy(ctypes.byref(st), TO_REF)
    for _ in range(max_steps):
        lib.difftest_exec(1)
        lib.difftest_regcpy(ctypes.byref(st), TO_DUT)
        if lib.difftest_halted():
            break
    return st


def main():
    src = sys.argv[1]
    name = os.path.splitext(os.path.basename(src))[0]
    binp = sysrun.assemble(src, name)
    dut = sysrun.run(binp)
    st = run_xemu(binp)
    bad = []
    for i in range(16):
        if int(dut["gpr"][i], 16) != st.gpr[i]:
            bad.append(f"gpr[{i}] dut=0x{int(dut['gpr'][i],16):x} xemu=0x{st.gpr[i]:x}")
    if int(dut["rip"], 16) != st.rip:
        bad.append(f"rip dut=0x{int(dut['rip'],16):x} xemu=0x{st.rip:x}")
    mask = 0xfd5
    if (int(dut["flags"], 16) & mask) != (st.rflags & mask):
        bad.append(f"flags dut=0x{int(dut['flags'],16):x} xemu=0x{st.rflags:x}")
    if bad:
        print(f"FAIL {name}")
        for b in bad[:20]:
            print("  " + b)
        sys.exit(1)
    print(f"PASS {name}: dut_rip=0x{int(dut['rip'],16):x} xemu_rip=0x{st.rip:x}")


if __name__ == "__main__":
    main()
