#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""NEMU-style post-processing of XEMU instruction traces (L4 tooling).

The emulator can dump the executed PC stream:

    build/xemu-linux --kernel build/linux/vmlinux ... \\
        --trace-pc=/tmp/pc.txt          # one PC per executed instruction
    build/xemu-linux ... --trace-bb=/tmp/bb.txt   # one PC per basic block

This tool turns those raw streams into something a kernel debugger can use:

    itrace   PC  symbol+offset  disassembly        (NEMU itrace style)
    ftrace   reconstruct the call/return tree, or --top for a cheap profile
    mtrace   --hot-pc N: the hottest PCs with symbols (the emulator does not
             log data addresses yet, so this is the memory-tracing substitute)
    stats    instruction/block counts + top symbols by self count

Everything streams: a 100M-line trace is read once (twice at most) and never
held in memory as raw text.  Disassembly is fetched per traced function with
`llvm-objdump --disassemble-symbols`, which is much cheaper than dumping the
whole kernel (a 2M-instruction boot trace touches only ~170 functions).

Examples
--------
    # 2M-instruction boot trace, then annotate it
    build/xemu-linux --kernel build/linux/vmlinux --cmdline=console=ttyS0 \\
        --initrd build/linux/initramfs.cpio.gz -b --max-steps=2000000 \\
        --trace-pc=/tmp/pc.txt
    python3 tools/xemu_itrace.py itrace --trace /tmp/pc.txt --fold
    python3 tools/xemu_itrace.py ftrace --trace /tmp/pc.txt
    python3 tools/xemu_itrace.py ftrace --trace /tmp/pc.txt --top 20
    python3 tools/xemu_itrace.py mtrace --trace /tmp/pc.txt --hot-pc 20
    python3 tools/xemu_itrace.py stats  --trace /tmp/pc.txt

PCs that are not in the kernel image are reported as `<user 0x...>` (low
half) or `<unknown 0x...>` and never crash the tool.
"""

import argparse
import bisect
import collections
import heapq
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_VMLINUX = os.path.join(ROOT, "build", "linux", "vmlinux")

# __START_KERNEL_map: physical early-boot PCs (0x1000000...) alias the kernel
# image at 0xffffffff8...; we derive the exact delta from the symbol table.
KPHYS_FALLBACK = 0xFFFFFFFF80000000
U64 = (1 << 64) - 1

# Above this many functions, one `--disassemble-symbols` command is unwieldy
# and disassembling the whole image is cheaper.
MAX_FILTER_SYMBOLS = 3000


class ToolError(Exception):
    pass


# --------------------------------------------------------------------------
# Trace streaming
# --------------------------------------------------------------------------

def iter_pcs(path, max_lines=0):
    """Yield integer PCs from a --trace-pc/--trace-bb file.

    Blank and malformed lines are skipped (a truncated last line must not
    crash anything); `int(s, 16)` accepts the 0x prefix XEMU writes.
    """
    n = 0
    with open(path, "r", buffering=1 << 20, errors="replace") as f:
        for line in f:
            n += 1
            if max_lines and n > max_lines:
                return
            s = line.strip()
            if not s:
                continue
            try:
                yield int(s, 16)
            except ValueError:
                continue


def scan_trace(path, max_lines):
    """One streaming pass: per-PC counts plus line quality statistics."""
    counts = {}
    bad = 0
    lines = 0
    get = counts.get
    with open(path, "r", buffering=1 << 20, errors="replace") as f:
        for line in f:
            lines += 1
            if max_lines and lines > max_lines:
                lines -= 1
                break
            s = line.strip()
            if not s:
                continue
            try:
                pc = int(s, 16)
            except ValueError:
                bad += 1
                continue
            counts[pc] = get(pc, 0) + 1
    return counts, lines, bad


# --------------------------------------------------------------------------
# Symbols (llvm-nm -n -S)
# --------------------------------------------------------------------------

class SymGroup:
    __slots__ = ("addr", "end", "name", "names", "size")

    def __init__(self, addr, end, name, names, size):
        self.addr = addr
        self.end = end
        self.name = name       # display name (largest-sized alias)
        self.names = names     # every alias at this address
        self.size = size


class SymbolTable:
    def __init__(self, vmlinux, nm="llvm-nm", objdump="llvm-objdump"):
        if not os.path.exists(vmlinux):
            raise ToolError("vmlinux not found: %s" % vmlinux)
        try:
            out = subprocess.run(
                [nm, "-n", "-S", "--defined-only", vmlinux],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                check=False,
            )
        except OSError as e:
            raise ToolError("cannot run %s: %s" % (nm, e))
        if out.returncode != 0:
            raise ToolError("%s failed: %s" % (nm, out.stderr.strip()))

        self.groups = []
        self.addrs = []
        self.by_name = collections.defaultdict(list)
        self.abs_syms = {}
        text_syms = []

        for line in out.stdout.splitlines():
            parts = line.split(None, 3)
            if len(parts) < 4:
                continue
            try:
                addr = int(parts[0], 16)
                size = int(parts[1], 16)
            except ValueError:
                continue
            typ, name = parts[2], parts[3].strip()
            if typ in "aA":
                self.abs_syms.setdefault(name, addr)
            elif typ in "tTwWi":
                text_syms.append((addr, size, name))

        if not text_syms:
            raise ToolError("no text symbols in %s (stripped image?)" % vmlinux)

        # A function (sized symbol) must win over zero-sized local labels that
        # sit inside it, otherwise top-symbol counts fragment into labels.
        self.sized_groups = []
        self.sized_addrs = []

        text_syms.sort(key=lambda t: (t[0], t[2]))
        i = 0
        n = len(text_syms)
        while i < n:
            addr = text_syms[i][0]
            members = []
            while i < n and text_syms[i][0] == addr:
                members.append(text_syms[i])
                i += 1
            max_size = max(m[1] for m in members)
            if max_size > 0:
                end = addr + max_size
            elif i < n:
                end = text_syms[i][0]
            else:
                end = addr + 16
            # Display name: a real (sized) symbol beats zero-sized aliases;
            # among sized aliases prefer the larger one, then the shorter name.
            primary = max(members, key=lambda m: (m[1], -len(m[2]), m[2]))
            names = [m[2] for m in members]
            grp = SymGroup(addr, end, primary[2], names, max_size)
            self.groups.append(grp)
            self.addrs.append(addr)
            for nm_ in names:
                self.by_name[nm_].append(grp)
            if max_size > 0:
                self.sized_groups.append(grp)
                self.sized_addrs.append(addr)

        self.image_lo = min(g.addr for g in self.sized_groups)
        self.image_hi = max(g.end for g in self.sized_groups)

        # Executable section ranges, used as labels for symbol-less code such
        # as .altinstr_aux (alternatives patched into the image at boot).
        self.sections = []
        self.section_addrs = []
        try:
            hdr = subprocess.run([objdump, "-h", vmlinux],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, check=False)
            for line in hdr.stdout.splitlines():
                parts = line.split()
                if len(parts) < 5 or not parts[0].isdigit():
                    continue
                if "CODE" not in line and "TEXT" not in line:
                    continue
                try:
                    size = int(parts[2], 16)
                    vma = int(parts[3], 16)
                except ValueError:
                    continue
                if size > 0:
                    self.sections.append((vma, vma + size, parts[1]))
        except OSError:
            pass
        self.sections.sort()
        self.section_addrs = [sec[0] for sec in self.sections]

        # Early boot runs the kernel image at its physical load address, with
        # the same code; derive the virtual-minus-physical delta.
        self.kphys_offset = KPHYS_FALLBACK
        phys = self.abs_syms.get("phys_startup_64")
        start = self.by_name.get("startup_64")
        if phys is not None and start and start[0].addr > phys:
            self.kphys_offset = start[0].addr - phys

    def at(self, pc):
        """Return (group, offset) for a *sized* function containing pc.

        Only sized symbols are used for attribution.  End markers such as
        `_etext`/`_einittext` are zero-sized and would otherwise own an
        arbitrary stretch of code; local labels such as
        `secondary_startup_64_no_verify` are aliases inside a real function.
        """
        i = bisect.bisect_right(self.sized_addrs, pc) - 1
        if i >= 0:
            g = self.sized_groups[i]
            if pc < g.end:
                return g, pc - g.addr
        return None

    def group_at(self, pc):
        """Nearest symbol group (including zero-sized labels) covering pc.

        Used only to pick objdump symbol names: a zero-sized alias splits a
        function in objdump's `--disassemble-symbols` output, so the alias
        itself must be requested too.
        """
        i = bisect.bisect_right(self.addrs, pc) - 1
        if i >= 0:
            g = self.groups[i]
            if pc < g.end:
                return g, pc - g.addr
        return None

    def resolve(self, pc):
        """Map a trace PC to (virtual_pc, (group, offset), kind).

        kind: 0 = kernel virtual, 1 = kernel physical alias,
              2 = user space, 3 = unknown.
        """
        hit = self.at(pc)
        if hit is not None:
            return pc, hit, 0
        if pc < KPHYS_FALLBACK:
            cand = (pc + self.kphys_offset) & U64
            hit = self.at(cand)
            if hit is not None:
                return cand, hit, 1
        if 0x400000 <= pc < 0x800000000000:
            return None, None, 2
        return None, None, 3

    def image_virtual(self, pc):
        """Virtual address to disassemble for a trace PC, or None.

        Works for symbolised code, for symbol-less code inside the image
        (e.g. .altinstr_aux), and for the physical early-boot alias.  Returns
        None for user-space / out-of-image PCs.
        """
        v, hit, kind = self.resolve(pc)
        if hit is not None:
            return v
        if kind == 2:
            return None
        if self.image_lo <= pc < self.image_hi:
            return pc
        if pc < KPHYS_FALLBACK:
            cand = (pc + self.kphys_offset) & U64
            if self.image_lo <= cand < self.image_hi:
                return cand
        return None

    def label(self, pc):
        """Human label for a trace PC, never raising."""
        v, hit, kind = self.resolve(pc)
        if hit is None:
            if kind == 2:
                return "<user 0x%x>" % pc
            iv = self.image_virtual(pc)
            if iv is not None:
                sec = self.section_at(iv)
                if sec is not None:
                    lab = "%s+0x%x" % sec
                    if iv != pc:
                        lab += " [phys]"
                    return lab
            return "<unknown 0x%x>" % pc
        g, off = hit
        lab = "%s+0x%x" % (g.name, off)
        if kind == 1:
            lab += " [phys]"
        return lab

    def name_at(self, pc):
        hit = self.at(pc)
        if hit is not None:
            return hit[0].name
        sec = self.section_at(pc)
        return sec[0] if sec is not None else None

    def section_at(self, vaddr):
        """Return (section name, offset) for executable section, else None."""
        i = bisect.bisect_right(self.section_addrs, vaddr) - 1
        if i >= 0:
            lo, hi, name = self.sections[i]
            if lo <= vaddr < hi:
                return name, vaddr - lo
        return None

    def attribution(self, pc):
        """Symbol/section name used for profiles; never None."""
        v, hit, kind = self.resolve(pc)
        if hit is not None:
            return hit[0].name
        if kind == 2:
            return "<user>"
        iv = self.image_virtual(pc)
        if iv is not None:
            sec = self.section_at(iv)
            if sec is not None:
                return sec[0]
        return "<unknown>"

    def ranges_for_name(self, name):
        if name in self.by_name:
            return [(g.addr, g.end) for g in self.by_name[name]]
        return self.ranges_for_section(name)

    def ranges_for_section(self, name):
        return [(lo, hi) for lo, hi, sec in self.sections if sec == name]


# --------------------------------------------------------------------------
# Disassembly (llvm-objdump -d --no-show-raw-insn), restricted to traced code
# --------------------------------------------------------------------------

_INSN_RE = re.compile(r"^([0-9a-fA-F]+):([ \t]+.*)$")
_CALL_TARGET_RE = re.compile(r"0x([0-9a-fA-F]+)")


class Disasm:
    def __init__(self):
        self.info = {}   # virtual pc -> (length, kind, target)
        self.text = {}   # virtual pc -> instruction text (no address/bytes)

    def __len__(self):
        return len(self.info)


def _classify(text):
    """kind: 0 normal, 1 call, 2 ret; plus direct call target (or None)."""
    toks = text.split(None, 1)
    mn = toks[0] if toks else ""
    if mn.startswith("call"):
        rest = toks[1] if len(toks) > 1 else ""
        if rest.startswith("*"):
            return 1, None
        m = _CALL_TARGET_RE.search(rest)
        return 1, (int(m.group(1), 16) if m else None)
    if mn in ("ret", "retq", "retl"):
        return 2, None
    return 0, None


def _parse_insn(line):
    """Return (addr, text) for an instruction line, else None."""
    m = _INSN_RE.match(line)
    if not m:
        return None
    try:
        addr = int(m.group(1), 16)
    except ValueError:
        return None
    text = m.group(2).strip()
    if not text:
        return None
    return addr, text


def _objdump_cmd(objdump, vmlinux, names=None, start=None, stop=None):
    cmd = [objdump, "-d", "--no-show-raw-insn"]
    if names is not None:
        cmd.append("--disassemble-symbols=" + ",".join(names))
    if start is not None and stop is not None:
        cmd.append("--start-address=0x%x" % start)
        cmd.append("--stop-address=0x%x" % stop)
    cmd.append(vmlinux)
    return cmd


def _stream_disasm(proc, wanted, disasm):
    """Parse objdump output, keeping only instructions at `wanted` addresses.

    Instruction length is the delta to the next disassembled address in the
    same contiguous run (we asked objdump for --no-show-raw-insn).  The last
    instruction of a run has unknown length and is recorded as length 1.
    """
    prev = None  # (addr, text)
    found = set()

    def flush():
        if prev is None:
            return
        addr, text = prev
        if addr in wanted and addr not in disasm.info:
            kind, target = _classify(text)
            disasm.info[addr] = (1, kind, target)
            disasm.text[addr] = text
            found.add(addr)

    for line in proc.stdout:
        if line.startswith("Disassembly of section"):
            flush()
            prev = None
            continue
        parsed = _parse_insn(line)
        if parsed is None:
            continue
        addr, text = parsed
        if prev is not None:
            delta = addr - prev[0]
            if prev[0] in wanted and prev[0] not in disasm.info:
                length = delta if 0 < delta <= 15 else 1
                kind, target = _classify(prev[1])
                disasm.info[prev[0]] = (length, kind, target)
                disasm.text[prev[0]] = prev[1]
                found.add(prev[0])
        prev = (addr, text)
    flush()
    proc.wait()
    return found


def _run_and_stream(cmd, wanted, disasm):
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, bufsize=1 << 20,
        )
    except OSError as e:
        raise ToolError("cannot run %s: %s" % (cmd[0], e))
    found = _stream_disasm(proc, wanted, disasm)
    err = proc.stderr.read()
    if proc.returncode != 0:
        raise ToolError("%s failed (rc=%d): %s" %
                        (cmd[0], proc.returncode, err.strip()[:400]))
    return found


def _clusters(pcs, gap=64):
    """Group sorted PCs into tight [lo,hi) ranges for a range disassembly."""
    it = iter(sorted(pcs))
    cur = [next(it)]
    for p in it:
        if p - cur[-1] <= gap:
            cur.append(p)
        else:
            yield cur
            cur = [p]
    yield cur


def _full_scan(vmlinux, objdump, wanted, disasm, why):
    lo = min(wanted)
    hi = max(wanted) + 16
    sys.stderr.write("xemu_itrace: %s; disassembling [0x%x,0x%x)\n" %
                     (why, lo, hi))
    disasm.info.clear()
    disasm.text.clear()
    _run_and_stream(_objdump_cmd(objdump, vmlinux, start=lo, stop=hi),
                    wanted, disasm)


def load_disasm(vmlinux, symtab, wanted_virtual, objdump="llvm-objdump"):
    """Build a {pc: (len, kind, target)} index for the traced PCs only.

    Instead of dumping the whole kernel we ask objdump for just the functions
    that contain traced instructions.  Zero-sized aliases inside a function
    split objdump's per-symbol output, so the covering alias is requested as
    well; code with no symbol at all gets a tight address-range disassembly.
    """
    disasm = Disasm()
    wanted = set(wanted_virtual)
    if not wanted:
        return disasm

    # Which symbol names cover the traced PCs?
    names = set()
    covered = set()
    for v in wanted:
        sized = symtab.at(v)
        if sized is not None:
            names.add(sized[0].name)
            covered.add(v)
        ga = symtab.group_at(v)
        if ga is not None:
            g = ga[0]
            # Skip zero-sized end markers that "cover" a huge stretch of the
            # image (`_etext`, `_einittext`); range disassembly handles those.
            if g.size > 0 or (g.end - g.addr) <= 0x10000:
                names.add(g.name)
                covered.add(v)
    no_name = wanted - covered
    names = sorted(names)

    if len(names) > MAX_FILTER_SYMBOLS:
        _full_scan(vmlinux, objdump, wanted, disasm,
                   "%d functions touched (too many for symbol filtering)" %
                   len(names))
        return disasm

    joined = ",".join(names)
    if names and len(joined) < 120000 and not any("," in n for n in names):
        try:
            _run_and_stream(_objdump_cmd(objdump, vmlinux, names=names),
                            wanted, disasm)
        except ToolError as e:
            sys.stderr.write("xemu_itrace: note: %s; using full disassembly\n" % e)
            disasm.info.clear()
            disasm.text.clear()

    # Code without a symbol (e.g. .altinstr_aux): disassemble just its range.
    if no_name:
        clusters = list(_clusters(no_name))
        if len(clusters) > 16:
            _full_scan(vmlinux, objdump, wanted, disasm,
                       "%d symbol-less code clusters" % len(clusters))
            return disasm
        for cl in clusters:
            _run_and_stream(
                _objdump_cmd(objdump, vmlinux, start=cl[0], stop=cl[-1] + 16),
                wanted, disasm)

    missing = wanted - set(disasm.info)
    # A handful of mid-instruction PCs can never be found; a large gap means
    # the filtered path failed (unknown symbol, objdump quirk) -> full scan.
    if missing and (len(missing) > 50 or len(missing) * 100 > len(wanted)):
        _full_scan(vmlinux, objdump, wanted, disasm,
                   "%d traced PCs were not covered" % len(missing))

    missing = wanted - set(disasm.info)
    if missing:
        sample = ", ".join("0x%x" % a for a in sorted(missing)[:3])
        sys.stderr.write("xemu_itrace: warning: %d traced PCs have no "
                         "disassembly (e.g. %s)\n" % (len(missing), sample))
    return disasm


# --------------------------------------------------------------------------
# Output helpers
# --------------------------------------------------------------------------

class Writer:
    """Buffered line writer: many small writes are slow on 100M-line runs."""

    def __init__(self, stream=sys.stdout, chunk=4096):
        self.stream = stream
        self.buf = []
        self.chunk = chunk

    def line(self, s):
        self.buf.append(s)
        if len(self.buf) >= self.chunk:
            self.flush()

    def flush(self):
        if self.buf:
            self.stream.write("\n".join(self.buf) + "\n")
            self.buf.clear()


def fmt_pc(pc):
    return "0x%016x" % pc


def parse_range(text):
    if not text:
        return None
    for sep in (":", "-", ".."):
        if sep in text:
            a, b = text.split(sep, 1)
            break
    else:
        raise ToolError("--range needs LO:HI (hex), got %r" % text)
    try:
        lo, hi = int(a, 16), int(b, 16)
    except ValueError:
        raise ToolError("--range needs hex addresses, got %r" % text)
    if hi <= lo:
        raise ToolError("--range HI must be greater than LO")
    return lo, hi


def fmt_itrace_line(pc, symtab, disasm):
    label = symtab.label(pc)
    v = symtab.image_virtual(pc)
    text = disasm.text.get(v, "") if v is not None else ""
    return "%s  %-46s %s" % (fmt_pc(pc), label, text)


# --------------------------------------------------------------------------
# Modes
# --------------------------------------------------------------------------

def cmd_itrace(args, counts, symtab, disasm):
    rng = parse_range(args.range_)
    sym_ranges = None
    if args.symbol:
        sym_ranges = symtab.ranges_for_name(args.symbol)
        if not sym_ranges:
            raise ToolError("symbol not found in vmlinux: %s" % args.symbol)

    # Precompute the (expensive) symbolisation once per distinct PC: a 100M
    # line trace may only have a few hundred thousand distinct PCs.
    lines = {}
    for pc in counts:
        v = symtab.image_virtual(pc)
        text = disasm.text.get(v, "") if v is not None else ""
        lines[pc] = "%s  %-46s %s" % (fmt_pc(pc), symtab.label(pc), text)

    sym_ok = None
    if sym_ranges is not None:
        sym_ok = set()
        for pc in counts:
            v = symtab.image_virtual(pc)
            if v is not None and any(lo <= v < hi for lo, hi in sym_ranges):
                sym_ok.add(pc)

    def keep(pc):
        if rng is not None and not (rng[0] <= pc < rng[1]):
            return False
        if sym_ok is not None and pc not in sym_ok:
            return False
        return True

    out = Writer()
    run_pc = None
    run_n = 0
    run_line = None

    def flush_run():
        nonlocal run_pc, run_n, run_line
        if run_pc is not None:
            line = run_line
            if args.fold and run_n > 1:
                line += "  ... x%d" % run_n
            out.line(line)
        run_pc = None
        run_n = 0
        run_line = None

    for pc in iter_pcs(args.trace, args.max_lines):
        if not keep(pc):
            flush_run()
            continue
        if args.fold:
            if run_pc == pc:
                run_n += 1
                continue
            flush_run()
            run_pc = pc
            run_n = 1
            run_line = lines[pc]
        else:
            out.line(lines[pc])
    flush_run()
    out.flush()


def cmd_ftrace(args, counts, symtab, disasm):
    if args.top is not None:
        n = args.top if args.top > 0 else 20
        agg = collections.Counter()
        for pc, cnt in counts.items():
            agg[symtab.attribution(pc)] += cnt
        total = sum(counts.values()) or 1
        out = Writer()
        out.line("# ftrace --top %d: functions by self instruction count" % n)
        out.line("%12s  %7s  %s" % ("self", "pct", "symbol"))
        for name, cnt in heapq.nlargest(n, agg.items(), key=lambda kv: kv[1]):
            out.line("%12d  %6.2f%%  %s" % (cnt, 100.0 * cnt / total, name))
        out.flush()
        return

    out = Writer()
    out.line("# ftrace: call/return tree reconstructed from %s" % args.trace)
    stack = []           # frames: (function name, resume pc, call pc)
    unmatched_ret = 0
    depth_max = 0
    expect = None        # resume address expected after a ret
    root_printed = False
    events = 0
    vmap = {pc: symtab.image_virtual(pc) for pc in counts}

    for pc in iter_pcs(args.trace, args.max_lines):
        if expect is not None:
            if pc != expect:
                # Interrupt delivery or a longjmp between ret and resumption;
                # benign for a boot trace, so only count it.
                pass
            expect = None
        v = vmap.get(pc)
        if v is None:
            continue
        if not root_printed:
            out.line("%s @%s" % (symtab.attribution(pc), fmt_pc(pc)))
            root_printed = True
        info = disasm.info.get(v)
        if info is None:
            continue
        length, ikind, target = info
        if ikind == 1:
            tname = symtab.name_at(target) if target is not None else None
            if tname is None:
                tname = "?" if target is None else "<0x%x>" % target
            depth = len(stack)
            out.line("%s-> %s  (call @%s)" %
                     ("  " * depth, tname, fmt_pc(pc)))
            resume = (pc + length) & U64 if length else None
            # The frame belongs to the callee, so a ret prints the callee name.
            stack.append((tname, resume, pc))
            depth_max = max(depth_max, len(stack))
            events += 1
        elif ikind == 2:
            if stack:
                name, resume, _ = stack.pop()
                out.line("%s<- %s  (ret @%s)" %
                         ("  " * len(stack), name, fmt_pc(pc)))
                expect = resume
                events += 1
            else:
                unmatched_ret += 1
                if unmatched_ret <= 5:
                    out.line("<- (unmatched ret @%s)" % fmt_pc(pc))
        if events > 0 and events % 4096 == 0:
            out.flush()
    out.flush()
    if unmatched_ret or depth_max:
        sys.stderr.write(
            "xemu_itrace: ftrace: %d events, max depth %d, %d unmatched rets\n" %
            (events, depth_max, unmatched_ret))


def cmd_mtrace(args, counts, symtab, disasm):
    n = args.hot_pc if args.hot_pc and args.hot_pc > 0 else 20
    total = sum(counts.values()) or 1
    out = Writer()
    out.line("# mtrace --hot-pc %d: hottest PCs" % n)
    out.line("# (XEMU does not log data addresses yet; this is the memory-trace "
             "substitute)")
    out.line("%10s  %7s  %-18s  %-38s  %s" %
             ("count", "pct", "pc", "symbol", "disassembly"))
    for pc, cnt in heapq.nlargest(n, counts.items(), key=lambda kv: kv[1]):
        v = symtab.image_virtual(pc)
        label = symtab.label(pc)
        text = disasm.text.get(v, "") if v is not None else ""
        out.line("%10d  %6.2f%%  %-18s  %-38s  %s" %
                 (cnt, 100.0 * cnt / total, fmt_pc(pc), label, text))
    out.flush()


def cmd_stats(args, counts, symtab, disasm, lines, bad):
    total = sum(counts.values())
    out = Writer()
    out.line("# trace statistics: %s" % args.trace)
    if args.trace_kind == "bb":
        out.line("total entries:           %d" % total)
    else:
        out.line("total instructions:      %d" % total)
    out.line("trace lines:             %d" % lines)
    out.line("distinct PCs:            %d" % len(counts))
    if bad:
        out.line("malformed lines skipped: %d" % bad)

    if args.trace_kind == "bb":
        out.line("basic block entries:     %d" % total)
        out.line("distinct basic blocks:   %d" % len(counts))
        out.line("instructions/block:      n/a (bb trace)")
        blocks = total
    else:
        blocks = 0
        starts = set()
        prev = None
        prev_len = None
        unresolved = 0
        lens = {}
        for pc in counts:
            v = symtab.image_virtual(pc)
            info = disasm.info.get(v) if v is not None else None
            lens[pc] = info[0] if info is not None else None
        for pc in iter_pcs(args.trace, args.max_lines):
            plen = lens.get(pc)
            if prev is None:
                blocks += 1
                starts.add(pc)
            elif prev_len is not None and pc == prev + prev_len:
                pass
            else:
                blocks += 1
                starts.add(pc)
                if plen is None:
                    unresolved += 1   # user/out-of-image code: no length
            prev = pc
            prev_len = plen
        out.line("basic block entries:     %d" % blocks)
        out.line("distinct basic blocks:   %d" % len(starts))
        if blocks:
            out.line("instructions/block:      %.2f" % (float(total) / blocks))
        if unresolved:
            out.line("unresolved PCs:          %d (user/out-of-image code; "
                     "block lengths unknown)" % unresolved)

    agg = collections.Counter()
    for pc, cnt in counts.items():
        agg[symtab.attribution(pc)] += cnt
    out.line("")
    if args.trace_kind == "bb":
        out.line("# bb trace: counts below are basic-block entries, not instructions")
        out.line("top 20 symbols by block count:")
    else:
        out.line("top 20 symbols by self count:")
    out.line("%12s  %7s  %s" % ("self", "pct", "symbol"))
    for name, cnt in heapq.nlargest(20, agg.items(), key=lambda kv: kv[1]):
        out.line("%12d  %6.2f%%  %s" % (cnt, 100.0 * cnt / total, name))
    out.flush()


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="xemu_itrace.py",
        description="NEMU-style itrace/ftrace/mtrace/stats for XEMU PC traces.",
        epilog="PCs outside the kernel are shown as <user 0x...>; "
               "a truncated last trace line is ignored.",
    )
    ap.add_argument("mode", nargs="?", choices=("itrace", "ftrace", "mtrace",
                                                 "stats"),
                    help="what to do with the trace")
    ap.add_argument("--trace", "-t", help="PC trace file from --trace-pc=FILE "
                                          "or --trace-bb=FILE")
    ap.add_argument("--vmlinux", default=DEFAULT_VMLINUX,
                    help="kernel image with symbols (default: %(default)s)")
    ap.add_argument("--objdump", default="llvm-objdump",
                    help="disassembler (default: %(default)s)")
    ap.add_argument("--nm", default="llvm-nm", help="nm tool (default: %(default)s)")
    ap.add_argument("--max-lines", type=int, default=0,
                    help="stop after N trace lines (guard for huge traces; "
                         "0 = unlimited)")
    ap.add_argument("--trace-kind", choices=("pc", "bb"), default="pc",
                    help="pc trace (default) or --trace-bb output; affects stats")
    ap.add_argument("--fold", action="store_true",
                    help="itrace: collapse runs of the same PC into '... xN'")
    ap.add_argument("--symbol", help="itrace: only print PCs in this symbol")
    ap.add_argument("--range", dest="range_", metavar="LO:HI",
                    help="itrace: only print PCs in [LO,HI), hex")
    ap.add_argument("--top", nargs="?", type=int, const=20, default=None,
                    metavar="N",
                    help="ftrace: print the top N functions by self instruction "
                         "count instead of the call tree (default 20)")
    ap.add_argument("--hot-pc", nargs="?", type=int, const=20, default=None,
                    metavar="N",
                    help="mtrace: print the top N hottest PCs (default 20)")
    ap.add_argument("--stats", action="store_true",
                    help="print the stats report (equivalent to mode 'stats')")
    args = ap.parse_args(argv)

    if args.max_lines < 0:
        ap.error("--max-lines must be >= 0")
    if not args.trace:
        ap.error("--trace is required")
    if not os.path.exists(args.trace):
        ap.error("trace file not found: %s" % args.trace)

    mode = args.mode
    if args.stats:
        mode = "stats"
    elif mode is None:
        if args.hot_pc is not None:
            mode = "mtrace"
        elif args.top is not None:
            mode = "ftrace"
        else:
            ap.error("choose a mode: itrace | ftrace | mtrace | stats "
                     "(or pass --stats)")

    try:
        symtab = SymbolTable(args.vmlinux, args.nm, args.objdump)
        counts, lines, bad = scan_trace(args.trace, args.max_lines)
        if bad:
            sys.stderr.write("xemu_itrace: warning: skipped %d malformed "
                             "line(s)\n" % bad)

        wanted_v = set()
        for pc in counts:
            v = symtab.image_virtual(pc)
            if v is not None:
                wanted_v.add(v)

        if mode == "mtrace":
            disasm = load_disasm(args.vmlinux, symtab, wanted_v, args.objdump)
            cmd_mtrace(args, counts, symtab, disasm)
            return 0

        disasm = load_disasm(args.vmlinux, symtab, wanted_v, args.objdump)
        sys.stderr.write("xemu_itrace: %d %s, %d distinct PCs, %d disassembled\n"
                         % (lines,
                            "entries" if args.trace_kind == "bb" else "instructions",
                            len(counts), len(disasm)))

        if mode == "itrace":
            cmd_itrace(args, counts, symtab, disasm)
        elif mode == "ftrace":
            cmd_ftrace(args, counts, symtab, disasm)
        elif mode == "stats":
            cmd_stats(args, counts, symtab, disasm, lines, bad)
        else:
            ap.error("unknown mode %s" % mode)
    except ToolError as e:
        sys.stderr.write("xemu_itrace: error: %s\n" % e)
        return 2
    except BrokenPipeError:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
