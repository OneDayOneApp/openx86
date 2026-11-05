# `xemu_itrace.py` — NEMU-style itrace / ftrace / mtrace for XEMU

`tools/xemu_itrace.py` post-processes the raw PC traces written by the XEMU
runner.  It symbolises and disassembles the trace against a `vmlinux` with
`.symtab`, reconstructs the call/return tree, and produces the hot-PC and
function profiles you want when a 100M-instruction boot is too large to read.

Only the traced functions are disassembled (`llvm-objdump
--disassemble-symbols`), not the whole kernel: the 2M-instruction boot trace
below touches ~170 functions, so analysis starts in a couple of seconds.

```
itrace   PC  symbol+offset  disassembly        (NEMU itrace style)
ftrace   call/return tree, or --top for a cheap self-time profile
mtrace   --hot-pc N: hottest PCs with symbols  (no data-address log yet)
stats    instruction/block counts + top symbols by self count
```

Requirements: `llvm-nm`, `llvm-objdump` (override with `--nm` / `--objdump`),
and an ELF kernel with `.symtab` (`build/linux/vmlinux` by default).

---

## 1. Capturing a trace

The runner flags are `--trace-pc=FILE` and `--trace-bb=FILE` (the `=` form is
the real spelling; there is no separate `--trace-file` flag):

```bash
make build/xemu-linux

# one line per executed instruction (38 MB for 2M instructions)
./build/xemu-linux --kernel build/linux/vmlinux \
    --cmdline="console=ttyS0" --initrd build/linux/initramfs.cpio.gz \
    -b --max-steps=2000000 --trace-pc=/tmp/pc.txt

# one line per basic-block start (much smaller; jumps/calls/rets/faults/IRQs)
./build/xemu-linux --kernel build/linux/vmlinux \
    --cmdline="console=ttyS0" --initrd build/linux/initramfs.cpio.gz \
    -b --max-steps=2000000 --trace-bb=/tmp/bb.txt
```

A real 2M-instruction boot trace (the command above) produced:

```
$ wc -l /tmp/pc.txt
2000000 /tmp/pc.txt
$ ls -l /tmp/pc.txt
-rw-r--r--  1 ... 37978958 ... /tmp/pc.txt
$ head -3 /tmp/pc.txt
0x1000000
0x1000003
0x100000a
$ tail -2 /tmp/pc.txt
0xffffffff8169b4b2
0xffffffff8169b4b9
```

The trace starts at the physical entry `0x1000000` (identity-mapped early
boot) and switches to kernel virtual addresses (`0xffffffff8...`) later.  The
tool understands both; physical PCs are canonicalised to their virtual alias
using `phys_startup_64`/`startup_64` from the symbol table and printed with a
`[phys]` marker.

Capture cost on the development host: **1.9 s wall, 0.7 s user** for 2M
instructions and 38 MB of text (3.8 s under heavy concurrent load).
`--trace-bb` writes one line per basic block: for this boot that is 316 520
lines / 6.0 MB in 1.2 s, about 6x smaller than the PC trace.

### Bounded ring buffers

For "what happened right before this stop" debugging, `xemu/main.c` also has a
ring that is dumped at exit/stop instead of writing a huge file:

```
--trace-ring=N          keep the last N events (bare flag: 64)
--trace-ring=pc|exc|mem select the category
--trace-ring-kind=pc|exc|mem
--trace-ring-file=NAME  write the dump there instead of stderr
```

`pc` records executed PCs, `exc` records exception deliveries (vector, error,
faulting PC and sequence number), and `mem` records instructions with an
explicit memory operand together with their effective address.  Example:

```bash
$ ./build/xemu-linux -i build/tests/fuzz_1.bin -b --max-steps=1000 \
      --trace-ring=mem --trace-ring=6
---- trace-ring mem (6 entries, capacity 6) ----
  rip=0x4000ba ea=0x600000 op=1 sz=2
  rip=0x4000c8 ea=0x600068 op=1 sz=8
  ...
```

`--trace-pc=`, `--trace`, `--stop-at=`, `--watch-va=`, `--watch-mem=`,
`--stop-at-exc=`, `--pc-ring=` and the other existing diagnostics are unchanged.

---

## 2. `itrace` — annotated instruction stream

```bash
python3 tools/xemu_itrace.py itrace --trace /tmp/pc.txt
```

```
0x0000000001000000  startup_64+0x0 [phys]                          movq	%rsi, %r15
0x0000000001000003  startup_64+0x3 [phys]                          leaq	0x603f4e(%rip), %rsp # 0xffffffff81603f58 <init_thread_union+0x3f58>
0x000000000100000a  startup_64+0xa [phys]                          leaq	-0x11(%rip), %rdi # 0xffffffff81000000 <startup_64>
0x0000000001000011  startup_64+0x11 [phys]                         movl	$0xc0000101, %ecx # imm = 0xC0000101
0x0000000001000016  startup_64+0x16 [phys]                         leaq	0x633fe3(%rip), %rdx # 0xffffffff81634000 <fixed_percpu_data>
0x000000000100001d  startup_64+0x1d [phys]                         movl	%edx, %eax
0x000000000100001f  startup_64+0x1f [phys]                         shrq	$0x20, %rdx
0x0000000001000023  startup_64+0x23 [phys]                         wrmsr
0x0000000001000025  startup_64+0x25 [phys]                         callq	0xffffffff810004e0 <startup_64_setup_env>
0x00000000010004e0  startup_64_setup_env+0x0 [phys]                leaq	-0x7e9cefc0(%rdi), %rax
...
```

Options:

| option | meaning |
| --- | --- |
| `--symbol NAME` | only PCs inside that function/alias (works for physical aliases too) |
| `--range LO:HI` | only PCs in the half-open hex range |
| `--fold` | collapse consecutive runs of the same PC into `... xN` |
| `--max-lines N` | stop after N trace lines (guard for 100M-line files) |

```bash
# one function
python3 tools/xemu_itrace.py itrace --trace /tmp/pc.txt --symbol memset_orig
```
```
0xffffffff81186b30  memset_orig+0x0                                movq	%rdi, %r10
0xffffffff81186b33  memset_orig+0x3                                movzbl	%sil, %ecx
0xffffffff81186b37  memset_orig+0x7                                movabsq	$0x101010101010101, %rax # imm = 0x101010101010101
0xffffffff81186b41  memset_orig+0x11                               imulq	%rcx, %rax
...
```

A real kernel trace has almost no *consecutive* duplicate PCs (loops contain
several instructions; `rep movs`/`rep stos` are one emulated instruction), so
`--fold` mostly matters for tight self-loops and hand-written test images.
Synthetic run:

```
$ printf '0xffffffff81016c84\n0xffffffff81016c84\n0xffffffff81016c84\n0xffffffff81016c86\n' \
    > /tmp/fold.txt
$ python3 tools/xemu_itrace.py itrace --trace /tmp/fold.txt --fold
0xffffffff81016c84  quick_pit_calibrate+0x44                       rdtsc  ... x3
0xffffffff81016c86  quick_pit_calibrate+0x46                       shlq	$0x20, %rdx
```

---

## 3. `ftrace` — call/return tree and `--top` profile

`ftrace` classifies every traced PC as normal / `call` / `ret` from the
disassembly and keeps a model stack.  It prints one indented line per event;
return lines show the callee that is being left.

```bash
python3 tools/xemu_itrace.py ftrace --trace /tmp/pc.txt
```

```
# ftrace: call/return tree reconstructed from /tmp/pc.txt
startup_64 @0x0000000001000000
-> startup_64_setup_env  (call @0x0000000001000025)
<- startup_64_setup_env  (ret @0x0000000001000528)
-> verify_cpu  (call @0x0000000001000036)
<- verify_cpu  (ret @0x000000000100025c)
-> __startup_64  (call @0x0000000001000045)
<- __startup_64  (ret @0x00000000010004cf)
-> sev_verify_cbit  (call @0x0000000001000089)
<- sev_verify_cbit  (ret @0x0000000001000273)
-> early_setup_idt  (call @0xffffffff8100010d)
<- early_setup_idt  (ret @0xffffffff81010fa2)
-> memset  (call @0xffffffff8168e292)
<- memset  (ret @0xffffffff81186bbd)
...
```

Summary line on stderr:

```
xemu_itrace: ftrace: 127294 events, max depth 16, 0 unmatched rets
```

`ftrace --top` turns the same trace into a poor man's profiler (self = number
of executed instructions attributed to that function):

```bash
python3 tools/xemu_itrace.py ftrace --trace /tmp/pc.txt --top 10
```

```
# ftrace --top 10: functions by self instruction count
        self      pct  symbol
      600040   30.00%  quick_pit_calibrate
      390967   19.55%  __early_set_fixmap
      193029    9.65%  memset_orig
      138712    6.94%  __early_ioremap
      101058    5.05%  .altinstr_aux
       99816    4.99%  flush_tlb_one_kernel
       99190    4.96%  early_iounmap
       61246    3.06%  sort_r
       49908    2.50%  native_flush_tlb_one_user
       24460    1.22%  static_call_site_cmp
```

Caveats: `ftrace` needs a `--trace-pc` trace (a `--trace-bb` trace has no
individual call/ret instructions).  Indirect calls (`call *%rax`) print `?` as
the callee.  `lretq` is deliberately *not* treated as a return (early boot uses
it for the far jump into long mode).  Returns are matched LIFO; a return that
does not match the expected resume address (interrupt/longjmp) is tolerated and
only counted, not printed.

---

## 4. `mtrace` — hottest PCs (the memory-tracing substitute)

The runner does not log data addresses yet, so the useful first question about
a 100M-instruction trace is *which PCs/instructions dominate*.  `mtrace
--hot-pc N` answers exactly that, with symbol and disassembly:

```bash
python3 tools/xemu_itrace.py mtrace --trace /tmp/pc.txt --hot-pc 10
```

```
# mtrace --hot-pc 10: hottest PCs
# (XEMU does not log data addresses yet; this is the memory-trace substitute)
     count      pct  pc                  symbol                                  disassembly
     50000    2.50%  0xffffffff81016c84  quick_pit_calibrate+0x44                rdtsc
     50000    2.50%  0xffffffff81016c86  quick_pit_calibrate+0x46                shlq	$0x20, %rdx
     50000    2.50%  0xffffffff81016c8a  quick_pit_calibrate+0x4a                movq	%rdi, %rsi
     50000    2.50%  0xffffffff81016c8d  quick_pit_calibrate+0x4d                addl	$0x1, %ecx
     50000    2.50%  0xffffffff81016c90  quick_pit_calibrate+0x50                movq	%rdx, %rdi
     50000    2.50%  0xffffffff81016c93  quick_pit_calibrate+0x53                orq	%rax, %rdi
     50000    2.50%  0xffffffff81016c96  quick_pit_calibrate+0x56                cmpl	$0xc350, %ecx   # imm = 0xC350
     50000    2.50%  0xffffffff81016c9c  quick_pit_calibrate+0x5c                je	0xffffffff81016ca6 <quick_pit_calibrate+0x66>
     49999    2.50%  0xffffffff81016c9e  quick_pit_calibrate+0x5e                inb	$0x42, %al
     49999    2.50%  0xffffffff81016ca0  quick_pit_calibrate+0x60                inb	$0x42, %al
```

The PIT calibration loop (`quick_pit_calibrate`) owns exactly 600 040
instructions here, which is why it also tops the `--stats` list.

---

## 5. `stats` — the performance-work input

```bash
python3 tools/xemu_itrace.py stats --trace /tmp/pc.txt
```

```
# trace statistics: /tmp/pc.txt
total instructions:      2000000
trace lines:             2000000
distinct PCs:            5909
basic block entries:     316520
distinct basic blocks:   880
instructions/block:      6.32

top 20 symbols by self count:
        self      pct  symbol
      600040   30.00%  quick_pit_calibrate
      390967   19.55%  __early_set_fixmap
      193029    9.65%  memset_orig
      138712    6.94%  __early_ioremap
      101058    5.05%  .altinstr_aux
       99816    4.99%  flush_tlb_one_kernel
       99190    4.96%  early_iounmap
       61246    3.06%  sort_r
       49908    2.50%  native_flush_tlb_one_user
       24460    1.22%  static_call_site_cmp
       20730    1.04%  static_call_site_swap
       17648    0.88%  __static_call_transform.constprop.0
       17502    0.88%  format_decode
       14842    0.74%  arch_static_call_transform
       11699    0.58%  memcpy_orig
       11199    0.56%  early_memremap
       10490    0.52%  do_swap
        9912    0.50%  text_poke_early
        8638    0.43%  strcmp
        8612    0.43%  smp_scan_config
```

Definitions:

* **total instructions** — valid PCs (malformed/blank lines are skipped and
  reported).
* **distinct PCs** — unique instruction addresses executed.
* **basic block entries** — how many times control entered a new basic block.
  A new block starts when the next executed PC is not the fall-through of the
  previous instruction (`prev_pc + prev_len`).
* **distinct basic blocks** — unique block-start PCs.
* **instructions/block** — total instructions / block entries (dynamic average
  block length).
* **top symbols by self count** — instructions whose PC lies inside that
  function (aliases/end markers handled; symbol-less executable sections such
  as `.altinstr_aux` appear by section name).

`--stats` also works as a flag (`xemu_itrace.py --stats --trace ...`).  For a
`--trace-bb` file, pass `--trace-kind bb`; the report then treats every line as
a block start and says `instructions/block: n/a` (the instruction count is not
observable from a BB trace).  User-space PCs have no disassembly in the kernel
image, so their instruction lengths are unknown: `stats` counts each as a block
start and reports them as `unresolved PCs`; run `stats` on a kernel-only trace
(or on a BB trace) if you need exact user-mode block counts.

---

## 6. Robustness and cost

* PCs outside the kernel image are printed as `<user 0x...>` (low canonical
  half) or `<unknown 0x...>` and never crash the tool.  Kernel physical PCs
  (`0x1000000...`) are resolved through their virtual alias and marked
  `[phys]`.
* A truncated last line and malformed/blank lines are ignored; a warning gives
  the skipped count.
* Everything streams: the trace is read at most twice (once for the per-PC
  counts, once for the ordered modes); only a `{pc: count}` summary is kept in
  memory.  `--max-lines N` caps both passes.
* A 100M-line, 1.8 GB trace (the 2M trace repeated 50x) completes `stats` in
  **347 s wall / 122 s CPU** on the development host while this machine was
  heavily loaded; distinct-PC memory stayed at 5909 entries.  Expect much
  better on an idle machine.
* `--trace-bb` is strongly recommended for long runs.  In the trace above the
  2M instructions produce exactly 316 520 dynamic basic-block entries; the BB
  file is 6.0 MB instead of 38 MB, and `itrace`/`stats --trace-kind bb` work on
  it unchanged.  (The BB file's line count matching `stats`' block count is a
  useful cross-check of the tracing logic.)

Analysis times for the real 38 MB / 2M-line trace on the same host (wall; user
CPU in parentheses; the first three rows include ~1–2 s of symbol/disassembly
startup):

| mode | time |
| --- | --- |
| `mtrace --hot-pc 10` | 5.8 s (1.6 s) |
| `ftrace --top 10` | 3.0 s (1.7 s) |
| `itrace` (full 2M lines) | 3.0 s (2.3 s) |
| `stats` | 5.2 s (3.0 s) |
| `ftrace` (127k events) | 3.3 s (2.6 s) |

Most of the fixed cost is `llvm-nm` + disassembling the ~170 touched
functions; the marginal cost of the trace lines is a few seconds per 2M.

---

## 7. How it works (short version)

1. `llvm-nm -n -S --defined-only vmlinux` gives symbol addresses *and sizes*.
   Sized functions win over zero-sized labels/aliases; zero-sized end markers
   (`_etext`, `_einittext`) are ignored for attribution.  Executable section
   ranges are used as a fallback for symbol-less code.
2. `--trace-pc`/`--trace-bb` files are streamed once for `{pc: count}`.
3. For the PCs that actually appear, the tool asks
   `llvm-objdump -d --no-show-raw-insn --disassemble-symbols=<functions>`
   (chunked/range or full-image fallback if the filter cannot cover
   everything).  It keeps `{pc: (length, is_call, is_ret, call_target)}` and
   the instruction text.
4. `itrace`/`ftrace`/`stats` replay the file; `mtrace`/`--top` need only the
   counts.  Instruction length for block detection is the delta to the next
   disassembled instruction in the same contiguous run.
5. Physical early-boot PCs are mapped to virtual ones with the delta derived
   from `phys_startup_64` and `startup_64` (`0xffffffff80000000` here).
