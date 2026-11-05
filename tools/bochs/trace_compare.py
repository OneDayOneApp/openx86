#!/usr/bin/env python3
"""Diff an XEMU PC trace against a Bochs PC trace and symbolise the first
divergence.

XEMU produces its trace with
    ./build/xemu --kernel build/kernel/vmlinux-deb-sym.elf \
                 --max-steps=300000 --trace-pc=build/bochs/xemu.pc -b
which writes one `0x<hex>` PC per executed instruction.

Bochs traces come from tools/bochs/kernel_boot.py (`<out>.pc`) or directly
from its raw debugger log (`<out>.raw`, lines of the form
`(0).[123] [0xphys] 0008:0000000001000000 (unk. ctxt): ...`).  Both formats
are accepted for both arguments; Bochs' `rep` iterations are collapsed so the
two traces are compared at XEMU's instruction granularity.

Usage:
    python3 tools/bochs/trace_compare.py XEMU_TRACE BOCHS_TRACE
    python3 tools/bochs/trace_compare.py xemu.pc kernel_trace.raw --context 20

Symbols are resolved from build/kernel/system.map (override with --map).
Early-boot PCs are physical/identity addresses, so they are translated by
+0xffffffff80000000 before the symbol lookup.

Exit status: 0 = identical over the common prefix, 1 = diverged,
2 = could not read a trace.
"""

import argparse
import bisect
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_MAP = os.path.join(ROOT, "build", "kernel", "system.map")
KERNEL_MAP_BASE = 0xFFFFFFFF80000000
MEM_SIZE = 512 << 20

PC_RE = re.compile(r"^\s*(?:0x)?([0-9a-fA-F]+)\s*$")
BOCHS_RE = re.compile(
    r"^\(\d+\)\.\[\d+\] \[0x[0-9a-f]+\] ([0-9a-f]{4}):([0-9a-f]+) ")


def read_trace(path):
    """Return a list of PCs.  Accepts 0x<hex> lines and raw Bochs trace lines."""
    pcs = []
    raw_lines = 0
    try:
        f = open(path, "r", errors="replace")
    except OSError as e:
        raise SystemExit("cannot read %s: %s" % (path, e))
    with f:
        for line in f:
            raw_lines += 1
            m = BOCHS_RE.match(line)
            if m:
                pcs.append(int(m.group(2), 16))
                continue
            s = line.strip()
            if not s or s.startswith(("#", ";", "=")):
                continue
            # Bare 0x<hex> (XEMU --trace-pc and kernel_boot.py <out>.pc)
            m = PC_RE.match(s)
            if m and len(m.group(1)) <= 16:
                pcs.append(int(m.group(1), 16))
    return pcs


def collapse(pcs):
    out = []
    for pc in pcs:
        if not out or out[-1] != pc:
            out.append(pc)
    return out


class Symbols:
    def __init__(self, path):
        self.addrs = []
        self.names = []
        try:
            with open(path, "r", errors="replace") as f:
                for line in f:
                    parts = line.split()
                    if len(parts) < 3:
                        continue
                    try:
                        addr = int(parts[0], 16)
                    except ValueError:
                        continue
                    self.addrs.append(addr)
                    self.names.append(parts[2])
        except OSError:
            pass
        order = sorted(range(len(self.addrs)), key=lambda i: self.addrs[i])
        self.addrs = [self.addrs[i] for i in order]
        self.names = [self.names[i] for i in order]

    def __call__(self, pc):
        if not self.addrs:
            return ""
        lookup = pc + KERNEL_MAP_BASE if pc < KERNEL_MAP_BASE else pc
        i = bisect.bisect_right(self.addrs, lookup) - 1
        if i < 0:
            return ""
        base, name = self.addrs[i], self.names[i]
        if lookup - base > (1 << 20):
            return ""
        return "%s+0x%x" % (name, lookup - base) if lookup != base else name


def fmt(pc, sym):
    s = sym(pc)
    return "0x%-18x %s" % (pc, s) if s else "0x%x" % pc


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("xemu_trace")
    ap.add_argument("bochs_trace")
    ap.add_argument("--map", default=DEFAULT_MAP)
    ap.add_argument("--context", type=int, default=12,
                    help="number of PCs to show on each side (default 12)")
    ap.add_argument("--no-collapse", action="store_true",
                    help="do not merge consecutive identical PCs")
    ap.add_argument("--max", type=int, default=0,
                    help="only compare the first N PCs")
    ap.add_argument("--resync", type=int, default=0, metavar="WINDOW",
                    help="after a mismatch, look ahead for a WINDOW-PC run that "
                         "realigns the traces (insertion/deletion tolerant) and "
                         "report all further divergence events")
    args = ap.parse_args()

    xu = read_trace(args.xemu_trace)
    bo = read_trace(args.bochs_trace)
    if not args.no_collapse:
        xu = collapse(xu)
        bo = collapse(bo)
    if args.max:
        xu = xu[:args.max]
        bo = bo[:args.max]

    sym = Symbols(args.map)
    n = min(len(xu), len(bo))
    first = None
    for i in range(n):
        if xu[i] != bo[i]:
            first = i
            break

    print("xemu  trace: %s  (%d PCs)" % (args.xemu_trace, len(xu)))
    print("bochs trace: %s  (%d PCs)" % (args.bochs_trace, len(bo)))
    if first is None:
        if len(xu) == len(bo):
            print("IDENTICAL: all %d PCs match" % len(xu))
            return 0
        if len(xu) < len(bo):
            print("PREFIX MATCH: xemu trace is a prefix of bochs (%d vs %d PCs)"
                  % (len(xu), len(bo)))
        else:
            print("PREFIX MATCH: bochs trace is a prefix of xemu (%d vs %d PCs)"
                  % (len(bo), len(xu)))
        return 0

    print("FIRST DIFFERENCE at index %d (0-based)" % first)
    print("  xemu : %s" % fmt(xu[first], sym))
    print("  bochs: %s" % fmt(bo[first], sym))
    # optional insertion/deletion-aware realignment (CPUID detours, etc.)
    if args.resync > 0:
        W = args.resync
        L = 1024
        events = []
        i, j = first, first
        while i < n and j < len(bo):
            if xu[i] == bo[j]:
                i += 1
                j += 1
                continue
            best = None
            # case A: xemu is ahead; xu[i] shows up a little later in bochs
            for dj in range(0, min(L, len(bo) - j)):
                if bo[j + dj] == xu[i] and xu[i:i + W] == bo[j + dj:j + dj + W]:
                    best = (0, dj)
                    break
            # case B: bochs is ahead; bo[j] shows up a little later in xemu
            if best is None:
                for di in range(0, min(L, len(xu) - i)):
                    if xu[i + di] == bo[j] and xu[i + di:i + di + W] == bo[j:j + W]:
                        best = (di, 0)
                        break
            if best is None or best == (0, 0):
                events.append((i, j, "unrecoverable divergence"))
                break
            di, dj = best
            events.append((i, j, "realign: xemu +%d, bochs +%d" % (di, dj)))
            i += di
            j += dj
        print("")
        print("resync alignment (window=%d, %d divergence event(s)):" % (W, len(events)))
        for k, (i, j, why) in enumerate(events):
            print("  [%d] xemu idx %-7d %s" % (k, i, fmt(xu[i], sym)))
            print("      bochs idx %-7d %s   (%s)" % (j, fmt(bo[j], sym), why))

    lo = max(0, first - args.context)
    hi = min(n, first + args.context + 1)
    print("")
    print("%8s  %-46s  %-46s" % ("index", "xemu", "bochs"))
    print("%8s  %-46s  %-46s" % ("-" * 8, "-" * 46, "-" * 46))
    for i in range(lo, hi):
        mark = "  <<<" if i == first else ""
        print("%8d  %-46s  %-46s%s"
              % (i, fmt(xu[i], sym), fmt(bo[i], sym), mark))
    return 1


if __name__ == "__main__":
    sys.exit(main())
