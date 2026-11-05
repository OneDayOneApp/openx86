#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""Run a raw flat binary under Bochs (long mode) and dump architectural state.

The boot sector trampoline (boot.S) builds identity paging for 0-16MiB plus a
scratch page at 0xF0000000, enters long mode, copies the payload to 0x400000,
then parks. This driver resumes from the park address, sets RIP=0x400000 with a
clean register file, single-steps `--steps` instructions, and prints the state
in the same JSON shape as `build/xemu` / core_tb so the three can be diffed.
"""

import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BOCHS = os.path.join(ROOT, ".deps", "bochs-2.8", "bochs")
BUILD = os.path.join(ROOT, "build", "bochs")
ENTRY = 0x400000
HALT = 0xF0000000
PARK = 0x7D04          # BOOT_BASE + park offset (see boot.S symbols)
GPR_RE = re.compile(r"^(r[a-z0-9]+|rip|eflags)\s*:\s*(?:0x)?([0-9a-f_]+)", re.M)


def env_setup():
    cfg = os.path.join(BUILD, "bochsrc.txt")
    if not os.path.exists(cfg):
        raise SystemExit(f"missing {cfg}; run `make bochs-img` first")
    if not os.path.exists(BOCHS):
        raise SystemExit(f"missing {BOCHS}; run `make bochs-build` first")


def write_stub(path, flags=0x202, target=ENTRY):
    b = bytearray()
    b += b"\x48\xb8" + flags.to_bytes(8, "little")   # movabs $flags, %rax
    b += b"\x50"                                      # push %rax
    b += b"\x9d"                                      # popfq
    b += b"\x48\xb8" + target.to_bytes(8, "little")  # movabs $target, %rax
    b += b"\xff\xe0"                                  # jmp *%rax
    with open(path, "wb") as f:
        f.write(bytes(b))


def run(image, steps=0, until=None, timeout=120):
    env_setup()
    stub_path = os.path.join(BUILD, "setflags.bin")
    write_stub(stub_path)
    rc = os.path.join(BUILD, "run.rc")
    lines = ["set $auto_disassemble = 0", f"pb 0x{PARK:x}", "c"]
    # Bochs has no `set eflags` (documented but unimplemented), so load a
    # 24-byte stub that sets RFLAGS and jumps to the entry point.
    lines.append(f"loadmem \"{stub_path}\" 0x200000")
    lines.append("set rip = 0x200000")
    for reg in ["rax","rbx","rcx","rdx","rsi","rdi","rbp","r8","r9","r10","r11","r12","r13","r14","r15"]:
        lines.append(f"set {reg} = 0")
    lines.append("set rsp = 0x800000")
    lines.append("loadmem \"%s\" 0x%x" % (image, ENTRY))
    # 5 steps execute the stub (movabs, push, popf, movabs, jmp) and land on
    # the payload entry. The stub leaves the jump target in RAX, so clear the
    # registers XEMU starts from zero before stepping the payload.
    lines.append("s 5")
    for reg in ["rax","rbx","rcx","rdx","rsi","rdi","rbp",
                "r8","r9","r10","r11","r12","r13","r14","r15"]:
        lines.append(f"set {reg} = 0")
    lines.append("set rsp = 0x800000")
    # Stop as soon as the payload writes the XEMU halt MMIO address: the
    # boot trampoline mapped 0xF0000000 to scratch RAM, so Bochs would
    # otherwise run on past the end of the image.
    lines.append(f"watch w 0x{HALT:x} 8")
    lines.append("watch stop")
    if until is not None:
        lines.append(f"pb 0x{until:x}")
        lines.append("c")
    elif steps:
        lines.append(f"s {steps}")
    else:
        lines.append("pb 0x%x" % HALT)
        lines.append("c")
    lines += ["r", "quit"]
    with open(rc, "w") as f:
        f.write("\n".join(lines) + "\n")

    proc = subprocess.run(
        [BOCHS, "-f", os.path.join(BUILD, "bochsrc.txt"), "-q", "-rc", rc],
        capture_output=True, text=True, timeout=timeout)
    out = proc.stdout + proc.stderr
    regs, cur = {}, None
    for line in out.splitlines():
        m = GPR_RE.match(line.strip())
        if m:
            cur = m.group(1)
            regs[cur] = int(m.group(2).replace("_", ""), 16)
    if not regs:
        return None, out
    return regs, out


def main():
    args = sys.argv[1:]
    if not args:
        print("usage: run.py IMAGE [--steps N | --until ADDR]")
        return 2
    image = args[0]
    steps, until = 0, None
    i = 1
    while i < len(args):
        if args[i] == "--steps":
            steps = int(args[i + 1]); i += 2
        elif args[i] == "--until":
            until = int(args[i + 1], 0); i += 2
        else:
            i += 1
    regs, out = run(image, steps=steps, until=until)
    if regs is None:
        print(out[-2000:])
        return 1
    print(json.dumps({k: f"0x{v:016x}" for k, v in regs.items()}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
