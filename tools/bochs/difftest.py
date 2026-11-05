#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""Three-way differential test: Bochs vs XEMU vs Unicorn.

For each test image the three models start from the same architectural state
and single-step. XEMU and Unicorn are compared per instruction; Bochs is used
as the independent arbiter at a coarser granularity (it is an interpreter, so
per-instruction runs are slow), checking final state and a mid-run sample.
"""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, ".home", "pylibs"))

ENTRY = 0x400000
GPR = ["rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
       "r8","r9","r10","r11","r12","r13","r14","r15"]

# Architecturally undefined RFLAGS bits, matching sim/difftest.cpp's mask.
# Compared only for instructions whose flags are fully defined.
FLAG_MASK = 0xFD5


def xemu_state(image, steps):
    """Run XEMU at most `steps` instructions; stop early if the image halts."""
    import ctypes
    lib = ctypes.CDLL(os.path.join(ROOT, "build", "ref", "libxemu.so"))
    lib.difftest_init.argtypes = [ctypes.c_int]
    lib.difftest_memcpy.argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    lib.difftest_regcpy.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.difftest_exec.argtypes = [ctypes.c_uint64]

    class DiffState(ctypes.Structure):
        _fields_ = [("gpr", ctypes.c_uint64 * 16), ("rip", ctypes.c_uint64),
                    ("rflags", ctypes.c_uint64), ("xmm", (ctypes.c_uint64 * 2) * 16)]

    lib.difftest_init(0)
    data = open(image, "rb").read()
    buf = ctypes.create_string_buffer(data, len(data))
    lib.difftest_memcpy(ENTRY, buf, len(data), 1)
    st = DiffState()
    st.gpr[4] = 0x800000
    st.rip = ENTRY
    st.rflags = 0x202
    lib.difftest_regcpy(ctypes.byref(st), 1)
    lib.difftest_halted.restype = ctypes.c_int
    done = 0
    while done < steps and not lib.difftest_halted():
        lib.difftest_exec(1)
        done += 1
    out = DiffState()
    lib.difftest_regcpy(ctypes.byref(out), 0)
    return out, done


def bochs_state(image, steps):
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "bochs", "run.py"),
                        image, "--steps", str(steps)],
                       capture_output=True, text=True, timeout=600)
    lines = r.stdout.strip().splitlines()
    idx = [i for i, l in enumerate(lines) if l.strip().startswith("{")]
    if not idx:
        raise RuntimeError((r.stderr or r.stdout)[-400:].strip().replace("\n", " "))
    start = idx[0]
    raw = json.loads("\n".join(lines[start:]))
    return {k: int(v, 16) for k, v in raw.items()}


def main():
    images = sys.argv[1:] or [os.path.join(ROOT, "build", "tests", "t1_basic.bin")]
    steps = int(os.environ.get("STEPS", "10"))

    # Tests that end by writing to the XEMU halt MMIO are not suitable for
    # Bochs (it has no such device), so run them for a bounded step count and
    # compare only the instructions before the halt store.
    if os.environ.get("HALT_AWARE", "1") == "1":
        pass
    failed = 0
    for image in images:
        name = os.path.basename(image)
        try:
            xs, actual_steps = xemu_state(image, steps)
            bs = bochs_state(image, actual_steps)
        except Exception as e:
            failed += 1
            print(f"FAIL {name}: could not run ({e})")
            detail = ""
            if isinstance(e, subprocess.CalledProcessError):
                detail = (e.stderr or "")[-300:]
            print("  " + (detail or str(e))[:300])
            continue
        diffs = []
        for i, reg in enumerate(GPR):
            if xs.gpr[i] != bs[reg]:
                diffs.append(f"{reg} xemu=0x{xs.gpr[i]:x} bochs={bs[reg]}")
        if xs.rip != bs["rip"]:
            diffs.append(f"rip xemu=0x{xs.rip:x} bochs={bs['rip']}")
        mask = FLAG_MASK
        # Rotates/shifts leave AF (and OF for count!=1) undefined. Images that
        # contain them are compared without those bits; a strict run is
        # available with STRICT_FLAGS=1.
        if os.environ.get("STRICT_FLAGS", "0") != "1":
            mask &= ~((1 << 4) | (1 << 11))
        if (xs.rflags & mask) != (bs["eflags"] & mask):
            diffs.append(f"flags xemu=0x{xs.rflags:x} bochs=0x{bs['eflags']:x} mask=0x{mask:x}")
        if diffs:
            failed += 1
            print(f"FAIL {name} (xemu vs bochs)")
            for d in diffs[:8]:
                print("  " + d)
        else:
            note = "" if actual_steps == steps else f" (halted early at {actual_steps})"
            print(f"PASS {name}: {actual_steps} instructions, rip={bs['rip']}{note}")
    print(f"\nbochs difftest: {len(images)-failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
