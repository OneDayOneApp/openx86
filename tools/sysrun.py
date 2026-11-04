#!/usr/bin/env python3
"""Run a system-level assembly test on the Verilator model and check expectations."""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "sys")
TB = os.path.join(ROOT, "build", "obj_core", "core_tb")


def assemble(src, name):
    os.makedirs(BUILD, exist_ok=True)
    obj = os.path.join(BUILD, name + ".o")
    binp = os.path.join(BUILD, name + ".bin")
    subprocess.run(["clang", "-target", "x86_64-unknown-none-elf", "-c", src, "-o", obj], check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", obj, binp], check=True)
    return binp


def run(binp, cycles=2000000):
    out = subprocess.run([TB, binp, str(cycles)], capture_output=True, text=True, timeout=600)
    if out.returncode not in (0, 3):
        print(out.stdout, out.stderr)
        raise SystemExit(f"simulation failed rc={out.returncode}")
    return json.loads(out.stdout.strip().splitlines()[-1])


def main():
    src = sys.argv[1]
    name = os.path.splitext(os.path.basename(src))[0]
    exp_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(src), name + ".expect.json")
    binp = assemble(src, name)
    res = run(binp)
    exp = json.load(open(exp_path)) if os.path.exists(exp_path) else {}
    bad = []
    for k, v in exp.items():
        if k == "gpr":
            for idx, want in v.items():
                got = int(res["gpr"][int(idx)], 16)
                if got != int(want, 16):
                    bad.append(f"gpr[{idx}] got 0x{got:x} want {want}")
        elif k == "xmm":
            for idx, want in v.items():
                got = int(res["xmm"][int(idx)], 16)
                if got != int(want, 16):
                    bad.append(f"xmm[{idx}] got 0x{got:032x} want {want}")
        else:
            got = res.get(k)
            if isinstance(v, str) and isinstance(got, str):
                if got.lower() != v.lower():
                    bad.append(f"{k} got {got} want {v}")
            elif got != v:
                bad.append(f"{k} got {got} want {v}")
    if bad:
        print(f"FAIL {name}")
        for b in bad[:20]:
            print("  " + b)
        print(f"  cycles={res['cycles']} rip={res['rip']} fatalPc={res['fatalPc']} exit={res['exit']}")
        sys.exit(1)
    print(f"PASS {name}: cycles={res['cycles']} exit={res['exit']} fatal={res['fatal']} "
          f"fatalCode={res['fatalCode']} rip={res['rip']}")


if __name__ == "__main__":
    main()
