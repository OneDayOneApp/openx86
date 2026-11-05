#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""Boot a Linux vmlinux ELF under Bochs exactly the way xemu/boot.c does, and
capture an instruction-level PC trace that can be diffed against
`./build/xemu --kernel <elf> --trace-pc=FILE`.

What is replicated (see xemu/boot.c and xemu/main.c):

  * 512 MiB of RAM, zero-filled (xemu_mem is zero-initialised).
  * Every ELF PT_LOAD segment copied to its physical address:
        paddr = p_paddr if p_paddr != 0 and p_paddr < RAM
                else p_vaddr - 0xffffffff80000000 (kernel map) / p_vaddr
    The same normalisation is applied to e_entry (0xffffffff81000000 ->
    0x1000000), matching what `build/xemu` prints for this ELF.
  * boot_params (zero page) at 0x10000 with xemu's exact e820 map,
    cmdline at 0x20000, optional initramfs high in RAM.
  * Long mode with the identity map xemu builds (PML4 0x1000, PDPT 0x2000,
    PD 0x3000, 2 MiB pages over all of RAM), CR0=0x80000001, CR4=0x20,
    EFER=0x500, CR3=0x1000.
  * %rip = entry, %rsi = 0x10000, %rsp = 0x9f000, all other GPRs zero,
    RFLAGS = 0x202, CS = 0x08, SS = 0x10.

The boot stub lives in tools/bochs/boot_kernel.S.  The BIOS is skipped (the
debugger starts the CPU at the stub in real mode), so the ROM window is zero
too and the emulated memory image matches XEMU's low 1 MiB as well.

Two Bochs 2.8 quirks are worked around:

  * `loadmem <file> <addr>` only writes the *last* 4 KiB chunk of a file, always
    at <addr> (its loop forgets to advance the address).  Memory is therefore
    loaded in <=4 KiB page-aligned pieces.
  * The debugger counts every `rep` string iteration as one instruction, while
    XEMU executes a whole REP as one instruction.  `clear_bss` alone is an 18M
    iteration `rep stosb`, so the driver steps in chunks, detects a REP still
    running at a chunk boundary, and finishes it with tracing disabled via a
    temporary linear breakpoint on the next instruction.  Consecutive duplicate
    PCs are collapsed, giving XEMU's one-PC-per-instruction granularity.

Usage:
    python3 tools/bochs/kernel_boot.py                       # 300k instructions
    python3 tools/bochs/kernel_boot.py --steps 50000
    python3 tools/bochs/kernel_boot.py --stop-at 0xffffffff81288d5b
    python3 tools/bochs/kernel_boot.py --flat-image build/tests/t1_basic.bin

Outputs (default prefix build/bochs/kernel_trace):
    <out>.pc          collapsed PC trace, one 0x<hex> per line (XEMU format)
    <out>.raw         full Bochs debugger log (PC + disassembly + bytes)
    <out>.regs        register dump at the end
    <out>.bochs.log   Bochs device/error log
"""

import argparse
import bisect
import hashlib
import os
import re
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BOCHS = os.path.join(ROOT, ".deps", "bochs-2.8", "bochs")
BUILD = os.path.join(ROOT, "build", "bochs")
CHUNKS = os.path.join(BUILD, "chunks")
STUB_SRC = os.path.join(ROOT, "tools", "bochs", "boot_kernel.S")
STUB_OBJ = os.path.join(BUILD, "boot_kernel.o")
STUB_BIN = os.path.join(BUILD, "boot_kernel.bin")
ZERO_ROM = os.path.join(BUILD, "empty_bios_rom.bin")
ZERO_VGA_ROM = os.path.join(BUILD, "empty_vga_rom.bin")
DEFAULT_ELF = os.path.join(ROOT, "build", "kernel", "vmlinux-deb-sym.elf")
DEFAULT_MAP = os.path.join(ROOT, "build", "kernel", "system.map")

CLANG = "/opt/homebrew/opt/llvm/bin/clang"
OBJCOPY = "/opt/homebrew/opt/llvm/bin/llvm-objcopy"

MEM_SIZE = 512 << 20
KERNEL_MAP_BASE = 0xFFFFFFFF80000000
STUB_BASE = 0x40000
STUB_ENTRY_MAGIC = 0xDEADBEEFCAFEF00D
STUB_STACK_MAGIC = 0x1122334455667788
ZERO_PAGE = 0x10000
CMDLINE_ADDR = 0x20000
DEFAULT_CMDLINE = "console=ttyS0 earlyprintk=serial"
DEFAULT_STACK = 0x9F000
DEFAULT_FLAT_STACK = 0x800000
FLAT_LOAD = 0x400000
EARLY_ZERO_LIMIT = 0x8000000        # 128 MiB -- image + early boot allocator

TRACE_RE = re.compile(
    r"^\(\d+\)\.\[\d+\] \[0x[0-9a-f]+\] ([0-9a-f]{4}):([0-9a-f]+) "
    r"\(([^)]*)\): (.*) ; ([0-9a-f]*)\s*$")
BP_RE = re.compile(r"^\(\d+\) Breakpoint (\d+), 0x([0-9a-f]+)", re.M)
RIP_RE = re.compile(r"^rip:\s*([0-9a-f_]+)\s*$", re.M)
REP_PREFIXES = ("rep ", "repe ", "repne ", "repz ", "repnz ")


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------
def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def wr16(b, o, v):
    struct.pack_into("<H", b, o, v & 0xFFFF)


def wr32(b, o, v):
    struct.pack_into("<I", b, o, v & 0xFFFFFFFF)


def phys_addr(addr):
    """Kernel virtual -> physical, mirroring xemu's ELF loader fallback."""
    if addr >= KERNEL_MAP_BASE:
        return addr - KERNEL_MAP_BASE
    return addr


def run_checked(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("command failed: %s\n%s%s" % (" ".join(cmd), r.stdout, r.stderr))
    return r


def load_symbols(path):
    syms = []
    if not path or not os.path.exists(path):
        return syms
    with open(path, "r", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 3:
                continue
            try:
                addr = int(parts[0], 16)
            except ValueError:
                continue
            syms.append((addr, parts[2]))
    syms.sort()
    return syms


def symbolize(syms, addr, kernel=True):
    """Resolve addr to name+offset.  kernel=True also maps low (identity)
    addresses onto the 0xffffffff80000000-linked symbol map."""
    if not syms:
        return ""
    lookup = addr
    if kernel and addr < KERNEL_MAP_BASE:
        if not (0x1000000 <= addr < MEM_SIZE):
            return ""
        lookup = addr + KERNEL_MAP_BASE
    addrs = [s[0] for s in syms]
    i = bisect.bisect_right(addrs, lookup) - 1
    if i < 0:
        return ""
    base, name = syms[i]
    if lookup - base > (1 << 20):
        return ""
    return "%s+0x%x" % (name, lookup - base) if lookup != base else name


# --------------------------------------------------------------------------
# boot stub / bochsrc / zero page
# --------------------------------------------------------------------------
def build_stub(entry, stack):
    if not os.path.exists(STUB_SRC):
        raise SystemExit("missing %s" % STUB_SRC)
    run_checked([CLANG, "--target=i386-none-elf", "-c", STUB_SRC, "-o", STUB_OBJ])
    run_checked([OBJCOPY, "-O", "binary", STUB_OBJ, STUB_BIN])
    data = bytearray(open(STUB_BIN, "rb").read())
    for magic, value, what in ((STUB_ENTRY_MAGIC, entry, "entry"),
                               (STUB_STACK_MAGIC, stack, "stack")):
        off = data.find(struct.pack("<Q", magic))
        if off < 0:
            raise SystemExit("boot stub magic for %s not found" % what)
        data[off:off + 8] = struct.pack("<Q", value)
    patched = os.path.join(BUILD, "boot_kernel_patched.bin")
    with open(patched, "wb") as f:
        f.write(data)
    return patched


def ensure_zero_files():
    os.makedirs(CHUNKS, exist_ok=True)
    if not os.path.exists(ZERO_ROM) or os.path.getsize(ZERO_ROM) != 128 * 1024:
        with open(ZERO_ROM, "wb") as f:
            f.truncate(128 * 1024)
    if not os.path.exists(ZERO_VGA_ROM) or os.path.getsize(ZERO_VGA_ROM) != 36 * 1024:
        with open(ZERO_VGA_ROM, "wb") as f:
            f.truncate(36 * 1024)
    z = os.path.join(CHUNKS, "zeros.bin")
    if not os.path.exists(z) or os.path.getsize(z) != 4096:
        with open(z, "wb") as f:
            f.write(b"\x00" * 4096)
    return z


# Bochs' stock CPUID cannot be made bit-identical to XEMU's (Bochs insists on
# SEP/MMX/APIC for long mode and reports leaves 0x80000002..8 which XEMU
# returns zero for), but the vendor string and the optional feature bits can
# be brought close enough that Linux takes the same early branches.
XEMU_CPUID = """\
cpu: model=bx_generic, count=1, ips=200000000, reset_on_triple_fault=0
cpuid: vendor_string="X86-KMH-Core"
cpuid: brand_string="X86-KMH-Core"
cpuid: family=6
cpuid: model=6
cpuid: stepping=3
cpuid: simd=sse2
cpuid: mmx=1
cpuid: sep=1
cpuid: xsave=0
cpuid: xsaveopt=0
cpuid: movbe=0
cpuid: adx=0
cpuid: aes=0
cpuid: sha=0
cpuid: x86_64=1
cpuid: 1g_pages=0
cpuid: pcid=0
cpuid: fsgsbase=0
cpuid: smep=0
cpuid: smap=0
cpuid: mwait=0
cpuid: apic=xapic
"""

BOCHS_CPUID = """\
cpu: model=core2_penryn_t9600, count=1, ips=200000000, reset_on_triple_fault=0
"""


def write_bochsrc(path, log_path, cpu="xemu"):
    with open(path, "w") as f:
        f.write("# generated by tools/bochs/kernel_boot.py -- do not edit\n")
        f.write("megs: %d\n" % (MEM_SIZE >> 20))
        f.write("romimage: file=%s\n" % ZERO_ROM)
        f.write("vgaromimage: file=%s\n" % ZERO_VGA_ROM)
        f.write(XEMU_CPUID if cpu == "xemu" else BOCHS_CPUID)
        f.write("display_library: nogui\n")
        f.write("clock: sync=none\n")
        f.write("log: %s\n" % log_path)
        f.write("panic: action=fatal\n")
        f.write("error: action=report\n")


def build_boot_params(entry, cmdline, initrd_addr, initrd_size):
    zp = bytearray(4096)
    wr16(zp, 0x1FE, 0xAA55)                 # boot_flag
    zp[0x202:0x206] = b"HdrS"
    zp[0x1F1] = 4                           # setup_sects
    zp[0x210] = 0xFF                        # type_of_loader
    zp[0x211] = 0x81                        # loadflags
    wr16(zp, 0x206, 0x020F)                 # version
    wr32(zp, 0x230, 0x200000)               # kernel_alignment
    zp[0x234] = 1                           # relocatable
    wr16(zp, 0x236, 0x007F)                 # xloadflags
    wr32(zp, 0x260, 0)                      # init_size (no bzImage header)
    wr32(zp, 0x214, entry & 0xFFFFFFFF)     # code32_start
    wr32(zp, 0x228, CMDLINE_ADDR)           # cmdline ptr
    wr32(zp, 0x238, 2048)                   # cmdline_size
    wr32(zp, 0x22C, 0x37FFFFFF)             # initrd_addr_max
    if initrd_size:
        wr32(zp, 0x218, initrd_addr)
        wr32(zp, 0x21C, initrd_size)

    e820 = [
        (0x0, 0x9FC00, 1),
        (0x9FC00, 0xA0000 - 0x9FC00, 2),
        (0xF0000, 0x100000 - 0xF0000, 2),
        (0x100000, MEM_SIZE - 0x100000, 1),
    ]
    zp[0x1E8] = len(e820)
    off = 0x2D0
    for addr, size, typ in e820:
        struct.pack_into("<Q", zp, off, addr)
        struct.pack_into("<Q", zp, off + 8, size)
        wr32(zp, off + 16, typ)
        off += 20
    return bytes(zp), cmdline.encode() + b"\x00"


# --------------------------------------------------------------------------
# ELF parsing
# --------------------------------------------------------------------------
def parse_elf(path):
    data = open(path, "rb").read()
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise SystemExit("%s is not an ELF file" % path)
    if data[4] != 2:
        raise SystemExit("%s is not ELF64" % path)
    e_entry = u64(data, 0x18)
    phoff = u64(data, 0x20)
    phentsize = u16(data, 0x36)
    phnum = u16(data, 0x38)
    segs = []
    for i in range(phnum):
        o = phoff + i * phentsize
        if o + 56 > len(data):
            raise SystemExit("truncated program header table")
        if u32(data, o) != 1:               # PT_LOAD
            continue
        p_offset = u64(data, o + 0x08)
        p_vaddr = u64(data, o + 0x10)
        p_paddr = u64(data, o + 0x18)
        p_filesz = u64(data, o + 0x20)
        p_memsz = u64(data, o + 0x28)
        if p_paddr and p_paddr < MEM_SIZE:
            dest = p_paddr
        else:
            dest = phys_addr(p_vaddr)
        segs.append(dict(vaddr=p_vaddr, paddr=p_paddr, filesz=p_filesz,
                         memsz=p_memsz, dest=dest,
                         data=data[p_offset:p_offset + p_filesz]))
    return e_entry, segs


def page_chunks(addr, data):
    """Split (addr, data) so that no chunk crosses a 4 KiB page."""
    out = []
    off = 0
    while off < len(data):
        page_off = (addr + off) & 0xFFF
        n = min(0x1000 - page_off, len(data) - off)
        out.append((addr + off, data[off:off + n]))
        off += n
    return out


class ChunkStore:
    """Writes each distinct <=4 KiB piece into build/bochs/chunks and reuses
    it on later runs (keyed by the source image's identity)."""

    def __init__(self, stamp):
        self.stamp = stamp
        self.stamp_path = os.path.join(CHUNKS, "stamp.txt")
        # Chunk files are content-addressed (sha1 name), so switching images
        # cannot corrupt a run; stale entries are simply reused if they match
        # and are never invalidated behind a running Bochs.
        if self._read_stamp() != stamp:
            self._write_stamp(stamp)
        self.zeros = os.path.join(CHUNKS, "zeros.bin")

    def _read_stamp(self):
        try:
            return open(self.stamp_path).read().strip()
        except OSError:
            return ""

    def _write_stamp(self, stamp):
        with open(self.stamp_path, "w") as f:
            f.write(stamp)

    def put(self, data):
        if not data.strip(b"\x00"):
            return self.zeros
        h = hashlib.sha1(data).hexdigest()[:16]
        path = os.path.join(CHUNKS, h + ".bin")
        if not os.path.exists(path) or os.path.getsize(path) != len(data):
            with open(path, "wb") as f:
                f.write(data)
        return path


def build_load_plan(segments, store, zero_mode):
    """Return [(file, addr)] covering zeroed RAM + all segment bytes.

    Segments are chunked at page boundaries so the (buggy) loadmem writes are
    exact.  Zero pages that a segment fully covers are skipped."""
    items = []                     # (addr, bytes) segment pieces
    covered = set()                # page bases fully covered by segments
    for s in sorted(segments, key=lambda s: s["dest"]):
        if s["dest"] + s["filesz"] > MEM_SIZE:
            raise SystemExit("segment 0x%x+0x%x exceeds RAM" % (s["dest"], s["filesz"]))
        for addr, data in page_chunks(s["dest"], s["data"]):
            items.append((addr, data))
            if addr % 0x1000 == 0 and len(data) == 0x1000:
                covered.add(addr)
        if s["memsz"] > s["filesz"]:
            baddr = s["dest"] + s["filesz"]
            n = s["memsz"] - s["filesz"]
            for addr, data in page_chunks(baddr, b"\x00" * n):
                items.append((addr, data))
                if addr % 0x1000 == 0 and len(data) == 0x1000:
                    covered.add(addr)

    plan = []
    if zero_mode != "none":
        limit = EARLY_ZERO_LIMIT if zero_mode == "early" else MEM_SIZE
        for addr in range(0, limit, 0x1000):
            # Never skip the page-table pages: the boot stub rewrites them.
            if addr in covered and addr not in (0x1000, 0x2000, 0x3000):
                continue
            plan.append((store.zeros, addr))
    for addr, data in items:
        plan.append((store.put(data), addr))
    return plan


# --------------------------------------------------------------------------
# interactive Bochs debugger driver
# --------------------------------------------------------------------------
class BochsDbg:
    def __init__(self, bochsrc, raw_path, stdout_path):
        self.raw_path = raw_path
        self.pos = 0
        open(raw_path, "w").close()
        self.out_fp = open(stdout_path, "w")
        self.proc = subprocess.Popen(
            [BOCHS, "-f", bochsrc, "-q", "-dbglog", raw_path],
            stdin=subprocess.PIPE, stdout=self.out_fp,
            stderr=subprocess.STDOUT)

    def send(self, cmds):
        if isinstance(cmds, str):
            cmds = [cmds]
        try:
            payload = b"".join((c + "\n").encode() for c in cmds)
            self.proc.stdin.write(payload)
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass

    def _read_new(self):
        try:
            with open(self.raw_path, "rb") as f:
                f.seek(self.pos)
                return f.read()
        except FileNotFoundError:
            return b""

    def sync(self, timeout=120.0, what="command"):
        """Send `ptime` and wait for its marker; return output of prior cmds."""
        self.send("ptime")
        deadline = time.time() + timeout
        while True:
            data = self._read_new()
            idx = data.find(b"ptime:")
            if idx >= 0:
                end = data.find(b"\n", idx)
                if end >= 0:
                    text = data[:idx].decode("utf-8", "replace")
                    self.pos += end + 1
                    return text
            if self.proc.poll() is not None:
                text = data.decode("utf-8", "replace")
                self.pos += len(data)
                return text
            if time.time() > deadline:
                raise TimeoutError("Bochs did not answer %s within %.0fs" % (what, timeout))
            time.sleep(0.01)

    def cmd(self, cmds, timeout=120.0, what=None):
        self.send(cmds)
        return self.sync(timeout=timeout, what=what or str(cmds))

    def close(self):
        try:
            self.send("quit")
            time.sleep(0.05)
        except Exception:
            pass
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=10)
        self.out_fp.close()


def parse_trace(text):
    """Return [(pc, disasm, ilen)] for every trace line in text."""
    out = []
    for line in text.splitlines():
        m = TRACE_RE.match(line)
        if not m:
            continue
        out.append((int(m.group(2), 16), m.group(4).strip(),
                    len(m.group(5)) // 2))
    return out


def is_rep(disasm):
    d = disasm.lower()
    return any(d.startswith(p) for p in REP_PREFIXES)


def self_loop(disasm):
    d = disasm.lower()
    return d.startswith(("jmp .-2", "jmp .-0", "jmp $", "jmp .+0"))


def parse_rip(text):
    last = None
    for m in RIP_RE.finditer(text):
        last = m
    return None if last is None else int(last.group(1).replace("_", ""), 16)


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(
        description="Boot a vmlinux ELF under Bochs and trace PCs (XEMU-compatible).")
    ap.add_argument("--elf", default=DEFAULT_ELF, help="vmlinux ELF to boot")
    ap.add_argument("--flat-image", help="instead of an ELF, load a raw binary at 0x400000")
    ap.add_argument("--entry", help="override the entry point (hex)")
    ap.add_argument("--steps", type=int, default=300000,
                    help="number of traced guest instructions to record (default 300000)")
    ap.add_argument("--chunk", type=int, default=20000,
                    help="debugger steps per batch (default 20000)")
    ap.add_argument("--stop-at", default=None,
                    help="stop when RIP reaches this hex address")
    ap.add_argument("--cmdline", default=DEFAULT_CMDLINE)
    ap.add_argument("--initrd", default=None)
    ap.add_argument("--out", default=os.path.join(BUILD, "kernel_trace"),
                    help="output prefix (default build/bochs/kernel_trace)")
    ap.add_argument("--map", default=DEFAULT_MAP, help="system.map for symbol names")
    ap.add_argument("--timeout", type=float, default=900.0, help="overall wall-clock limit")
    ap.add_argument("--zero-ram", choices=("none", "early", "full"), default="early",
                    help="none: trust Bochs' fresh (zero) RAM; early: zero the "
                         "first 128 MiB; full: zero all 512 MiB (slow load)")
    ap.add_argument("--cpu", choices=("xemu", "core2"), default="xemu",
                    help="xemu: bx_generic with a CPUID profile close to XEMU's "
                         "synthetic CPU (default); core2: Bochs' core2_penryn model")
    ap.add_argument("--percpu", choices=("auto", "xemu", "kernel"), default="auto",
                    help="placement of the .data..percpu PT_LOAD (p_vaddr==p_paddr==0 "
                         "in the symbolized ELF).  xemu: physical 0 exactly like "
                         "xemu/boot.c; kernel: at the kernel's __per_cpu_load LMA; "
                         "auto: both (keeps xemu's image and gives the kernel a "
                         "valid early GDT, without which real hardware #GPs)")
    args = ap.parse_args()

    if not os.path.exists(BOCHS):
        raise SystemExit("missing Bochs binary %s" % BOCHS)
    os.makedirs(BUILD, exist_ok=True)
    ensure_zero_files()
    syms = load_symbols(args.map)

    out_prefix = args.out
    raw_path = out_prefix + ".raw"
    pc_path = out_prefix + ".pc"
    bochs_log = out_prefix + ".bochs.log"
    stdout_path = out_prefix + ".stdout"
    bochsrc = out_prefix + ".bochsrc"

    # ---- prepare guest memory image -------------------------------------
    if args.flat_image:
        img = open(args.flat_image, "rb").read()
        entry = int(args.entry, 16) if args.entry else FLAT_LOAD
        stack = DEFAULT_FLAT_STACK
        segs = [dict(dest=FLAT_LOAD, data=img, filesz=len(img), memsz=len(img),
                     vaddr=FLAT_LOAD, paddr=FLAT_LOAD)]
        extras = []
        initrd = None
        stamp = "flat:%s:%d:%d" % (
            os.path.abspath(args.flat_image), os.path.getsize(args.flat_image),
            os.path.getmtime(args.flat_image))
    else:
        e_entry, segs = parse_elf(args.elf)
        entry = int(args.entry, 16) if args.entry else phys_addr(e_entry)
        stack = DEFAULT_STACK
        # The symbolized ELF encodes .data..percpu as PT_LOAD vaddr=paddr=0.
        # xemu therefore loads it at physical 0, but the kernel expects it at
        # __per_cpu_load (the early GDT page lives at __per_cpu_load+0xb000).
        percpu_lma = None
        for a, n in syms:
            if n == "__per_cpu_load":
                percpu_lma = a - KERNEL_MAP_BASE
                break
        if args.percpu != "xemu" and percpu_lma is not None:
            extra_segs = []
            for s in segs:
                if s["paddr"] == 0 and s["vaddr"] == 0 and s["filesz"] > 0:
                    moved = dict(s, dest=percpu_lma)
                    if args.percpu == "kernel":
                        segs = [x for x in segs if x is not s]
                    extra_segs.append(moved)
            if extra_segs:
                segs = segs + extra_segs
                print("percpu: extra copy at __per_cpu_load = 0x%x" % percpu_lma)
            else:
                print("percpu: no PT_LOAD with vaddr=paddr=0 found")
        stamp = "elf:%s:%d:%d" % (
            os.path.abspath(args.elf), os.path.getsize(args.elf),
            os.path.getmtime(args.elf))
        extras = []
        initrd = None
        if args.initrd:
            data = open(args.initrd, "rb").read()
            addr = (MEM_SIZE - len(data) - 0x100000) & ~0xFFF
            p = os.path.join(BUILD, "initrd.bin")
            with open(p, "wb") as f:
                f.write(data)
            initrd = (p, addr, len(data))
            extras.append((addr, data))
            print("initrd: %d bytes -> 0x%x" % (len(data), addr))
        zp_bytes, cmd_bytes = build_boot_params(
            entry, args.cmdline,
            initrd[1] if initrd else 0, initrd[2] if initrd else 0)
        zp_path = os.path.join(BUILD, "boot_params.bin")
        cmd_path = os.path.join(BUILD, "cmdline.bin")
        with open(zp_path, "wb") as f:
            f.write(zp_bytes)
        with open(cmd_path, "wb") as f:
            f.write(cmd_bytes)
        extras.append((ZERO_PAGE, zp_bytes))
        extras.append((CMDLINE_ADDR, cmd_bytes))

    store = ChunkStore(stamp)
    all_segs = segs + [dict(dest=a, data=d, filesz=len(d), memsz=len(d),
                            vaddr=a, paddr=a) for a, d in extras]
    plan = build_load_plan(all_segs, store, args.zero_ram)

    stub = build_stub(entry, stack)
    write_bochsrc(bochsrc, bochs_log, args.cpu)

    setup = ["set $auto_disassemble = 0"]
    for path, addr in plan:
        setup.append('loadmem "%s" 0x%x' % (path, addr))
    # Load the stub last: the RAM zero-fill above covers its page too.
    setup.append('loadmem "%s" 0x%x' % (stub, STUB_BASE))
    setup += ["set cs = 0x4000", "set rip = 0"]
    print("image: %d loadmem chunks, zero-ram=%s" % (len(plan), args.zero_ram))

    dbg = BochsDbg(bochsrc, raw_path, stdout_path)
    t_start = time.time()
    t_trace_start = None
    pcs = []                 # raw PCs, one per Bochs step
    collapsed_count = 0      # PCs after merging consecutive duplicates
    last_pc = None
    stop_hit = False
    note = ""
    last_rip = None
    total_raw = 0
    elapsed = 0.0

    try:
        # ---- load + run the stub up to the kernel entry ------------------
        dbg.send(setup)
        text = dbg.sync(timeout=args.timeout, what="image load")
        if "Error" in text or "Can not open" in text:
            print(text[-2000:])
            raise SystemExit("Bochs reported an error while loading the image")

        if args.stop_at:
            stop_addr = int(args.stop_at, 16)
            if stop_addr >= (1 << 32):
                dbg.send("lb 0x%x" % stop_addr)
            else:
                dbg.send("pb 0x%x" % stop_addr)
        dbg.send("pb 0x%x" % entry)
        dbg.send("s 2000000")
        text = dbg.sync(timeout=args.timeout, what="boot stub")
        last_rip = parse_rip(dbg.cmd("r"))
        if last_rip != entry:
            print(text[-3000:])
            raise SystemExit("did not reach entry 0x%x (rip=0x%x); see %s"
                             % (entry, last_rip or 0, bochs_log))
        # Drop the entry breakpoint: a physical breakpoint costs a page-table
        # walk per instruction in the debugger epilog and would halve the rate.
        m = None
        for m in BP_RE.finditer(text):
            pass
        if m is not None:
            dbg.send("delete %s" % m.group(1))

        # ---- trace loop --------------------------------------------------
        dbg.send("trace on")
        dbg.send("trace-reg off")
        dbg.sync(timeout=30.0, what="trace on")
        t_trace_start = time.time()

        chunk = max(1, args.chunk)
        while collapsed_count < args.steps:
            if time.time() - t_start > args.timeout:
                note = "wall-clock timeout"
                break
            text = dbg.cmd("s %d" % chunk, timeout=args.timeout, what="step chunk")
            new = parse_trace(text)
            total_raw += len(new)
            pcs.extend(new)
            for pc, _d, _l in new:
                if pc != last_pc:
                    collapsed_count += 1
                    last_pc = pc

            bp = None
            for m in BP_RE.finditer(text):
                bp = int(m.group(2), 16)
            if bp is not None:
                if args.stop_at and bp == int(args.stop_at, 16):
                    stop_hit = True
                note = note or "breakpoint at 0x%x" % bp
                break
            if not new:
                note = "CPU stopped (halt or debugger exit)"
                break
            if len(new) < chunk:
                note = ("step batch cut short after %d instructions "
                        "(halt/exception/exit)" % len(new))
                break

            # A `rep` still running at the chunk boundary: finish it untraced
            # (see module docstring).  Consecutive duplicate PCs are collapsed
            # at the end, so this preserves XEMU's instruction granularity.
            if len(new) >= 2 and new[-1][0] == new[-2][0]:
                pc, disasm, ilen = new[-1]
                if is_rep(disasm) and ilen > 0:
                    rip = parse_rip(dbg.cmd("r"))
                    if rip == pc:
                        dbg.send("trace off")
                        dbg.sync(timeout=30.0, what="trace off")
                        dbg.send("lb 0x%x" % (pc + ilen))
                        dbg.send("c")
                        skip_text = dbg.sync(timeout=args.timeout, what="finish rep")
                        bm = None
                        for m in BP_RE.finditer(skip_text):
                            bm = m
                        if bm is None:
                            note = "rep skip at 0x%x did not hit 0x%x" % (pc, pc + ilen)
                            break
                        dbg.send("delete %s" % bm.group(1))
                        dbg.send("trace on")
                        dbg.sync(timeout=30.0, what="trace re-enable")
                elif self_loop(disasm):
                    note = "self-loop at 0x%x" % pc
                    break

        regs_text = dbg.cmd("r", timeout=60.0, what="register dump")
        last_rip = parse_rip(regs_text)
        with open(out_prefix + ".regs", "w") as f:
            f.write(regs_text)
    except TimeoutError as e:
        note = note or str(e)
    finally:
        elapsed = time.time() - t_start
        try:
            dbg.close()
        except Exception:
            pass

    # ---- collapse consecutive duplicate PCs (REP iterations) ------------
    collapsed = []
    for pc, _d, _l in pcs:
        if not collapsed or collapsed[-1] != pc:
            collapsed.append(pc)
    collapsed = collapsed[:args.steps]
    with open(pc_path, "w") as f:
        for pc in collapsed:
            f.write("0x%x\n" % pc)

    # ---- report ---------------------------------------------------------
    n = len(collapsed)
    print("=" * 72)
    print("boot: %s" % (args.flat_image or args.elf))
    print("entry=0x%x  stack=0x%x  cmdline=%r" % (entry, stack, args.cmdline))
    print("trace: %s  (%d PCs, %d raw Bochs steps)" % (pc_path, n, total_raw))
    print("raw debugger log: %s" % raw_path)
    if note:
        print("note: %s" % note)
    if stop_hit:
        print("stop-at 0x%x reached" % int(args.stop_at, 16))
        tail = collapsed[-200:]
        with open(out_prefix + ".stop-at.txt", "w") as f:
            f.write("stop-at 0x%x reached after %d traced instructions\n\n"
                    % (int(args.stop_at, 16), n))
            f.write("registers:\n")
            f.write(open(out_prefix + ".regs").read() if os.path.exists(out_prefix + ".regs") else "")
            f.write("\nlast %d PCs:\n" % len(tail))
            for k, pc in enumerate(tail):
                f.write("%8d  0x%-16x %s\n"
                        % (n - len(tail) + k, pc,
                           symbolize(syms, pc, kernel=not args.flat_image)))
        print("last %d PCs and registers written to %s.stop-at.txt" % (len(tail), out_prefix))
    if n:
        print("first 20 PCs:")
        for pc in collapsed[:20]:
            print("  0x%-16x %s" % (pc, symbolize(syms, pc, kernel=not args.flat_image)))
        print("last PC: 0x%-16x %s" % (collapsed[-1],
                                       symbolize(syms, collapsed[-1],
                                                 kernel=not args.flat_image)))
        if last_rip is not None:
            print("final RIP: 0x%-16x %s" % (last_rip,
                                             symbolize(syms, last_rip,
                                                       kernel=not args.flat_image)))
    trace_secs = (time.time() - t_trace_start) if t_trace_start else 0.0
    if trace_secs > 0:
        print("speed: %.0f traced instr/s over %.1fs of tracing (%.1fs total "
              "including image load)" % (n / trace_secs, trace_secs, elapsed))
    with open(out_prefix + ".meta", "w") as f:
        f.write("entry=0x%x\nsteps=%d\npcs=%d\nraw_steps=%d\nelapsed=%.3f\n"
                "trace_seconds=%.3f\nnote=%s\nstop_hit=%d\npercpu=%s\n"
                % (entry, args.steps, n, total_raw, elapsed,
                   (time.time() - t_trace_start) if t_trace_start else 0.0,
                   note, int(stop_hit), args.percpu))
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
