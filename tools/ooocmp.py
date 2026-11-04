#!/usr/bin/env python3
"""Compare the Stage-B out-of-order core against the Stage-A reference core."""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "ooo")
A_TB = os.path.join(ROOT, "build", "obj_core", "core_tb")
B_TB = os.path.join(ROOT, "build", "obj_ooo", "ooo_tb")
FLAG_MASK = 0xFD5


def assemble(src, name):
    os.makedirs(BUILD, exist_ok=True)
    obj = os.path.join(BUILD, name + ".o")
    binp = os.path.join(BUILD, name + ".bin")
    subprocess.run(["clang", "-target", "x86_64-unknown-none-elf", "-c", src, "-o", obj], check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", obj, binp], check=True)
    return binp


def run(tb, binp, limit):
    out = subprocess.run([tb, binp, str(limit)], capture_output=True, text=True, timeout=600)
    if out.returncode not in (0, 3):
        print(out.stdout, out.stderr)
        raise SystemExit(f"simulation failed rc={out.returncode}")
    return json.loads(out.stdout.strip().splitlines()[-1])


def main():
    src = sys.argv[1]
    name = os.path.splitext(os.path.basename(src))[0]
    binp = assemble(src, name)
    a = run(A_TB, binp, 2000000)
    b = run(B_TB, binp, 2000000)

    bad = []
    ga = [int(x, 16) for x in a["gpr"]]
    gb = [int(x, 16) for x in b["gpr"]]
    for i, (x, y) in enumerate(zip(ga, gb)):
        if x != y:
            bad.append(f"gpr[{i}] A=0x{x:016x} B=0x{y:016x}")
    if int(a["memhash"], 16) != int(b["memhash"], 16):
        bad.append(f"memhash A={a['memhash']} B={b['memhash']}")
    if int(a["rip"], 16) != int(b["rip"], 16):
        bad.append(f"rip A={a['rip']} B={b['rip']}")
    if (int(a["flags"], 16) & FLAG_MASK) != (int(b["flags"], 16) & FLAG_MASK):
        bad.append(f"flags A={a['flags']} B={b['flags']}")
    if b["halted"] != 1 or b["fatal"] != 0:
        bad.append(f"B halted={b['halted']} fatal={b['fatal']} pc={b['fatalPc']} code={b['fatalCode']}")
    for i, (x, y) in enumerate(zip(a["xmm"], b["xmm"])):
        if int(x, 16) != int(y, 16):
            bad.append(f"xmm[{i}] A={x} B={y}")

    if bad:
        print(f"FAIL {name}")
        for x in bad[:20]:
            print("  " + x)
        print(f"  A cycles={a['cycles']} B cycles={b['cycles']}")
        sys.exit(1)
    print(f"PASS {name}: A cycles={a['cycles']} OoO cycles={b['cycles']}")


if __name__ == "__main__":
    main()
