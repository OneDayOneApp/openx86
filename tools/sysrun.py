#!/usr/bin/env python3
"""Run a system-level assembly test on the Verilator model and check expectations."""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "sys")
TB = os.path.join(ROOT, "build", "obj_core", "core_tb")
REF = os.path.join(ROOT, "build", "ref", "libxemu.so")


def assemble(src, name):
    os.makedirs(BUILD, exist_ok=True)
    obj = os.path.join(BUILD, name + ".o")
    binp = os.path.join(BUILD, name + ".bin")
    subprocess.run(["clang", "-target", "x86_64-unknown-none-elf", "-c", src, "-o", obj], check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", obj, binp], check=True)
    return binp


def run(binp, cycles=2000000, diff=False):
    cmd = [TB, binp, str(cycles)]
    if diff:
        cmd += ["--diff", f"--ref-so={REF}"]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if out.returncode not in (0, 3):
        print(out.stdout, out.stderr)
        raise SystemExit(f"simulation failed rc={out.returncode}")
    return json.loads(out.stdout.strip().splitlines()[-1])


def main():
    args = [a for a in sys.argv[1:] if a != "--diff"]
    diff = "--diff" in sys.argv[1:]
    src = args[0]
    name = os.path.splitext(os.path.basename(src))[0]
    exp_path = args[1] if len(args) > 1 else os.path.join(os.path.dirname(src), name + ".expect.json")
    binp = assemble(src, name)
    res = run(binp)
    if diff:
        import xemu_sysrun as xs
        st = xs.run_xemu(binp)
        diffs = []
        for i in range(16):
            if int(res["gpr"][i], 16) != st.gpr[i]:
                diffs.append(f"gpr[{i}] dut=0x{int(res['gpr'][i],16):x} xemu=0x{st.gpr[i]:x}")
        if int(res["rip"], 16) != st.rip:
            diffs.append(f"rip dut=0x{int(res['rip'],16):x} xemu=0x{st.rip:x}")
        if (int(res["flags"], 16) & 0xfd5) != (st.rflags & 0xfd5):
            diffs.append(f"flags dut=0x{int(res['flags'],16):x} xemu=0x{st.rflags:x}")
        if diffs:
            print(f"FAIL {name} (xemu co-sim)")
            for d in diffs[:10]:
                print("  " + d)
            sys.exit(1)
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
