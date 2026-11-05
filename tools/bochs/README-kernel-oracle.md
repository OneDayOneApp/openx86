# Bochs instruction-level oracle for the XEMU Linux boot

`tools/bochs/kernel_boot.py` boots the same `vmlinux` ELF under Bochs 2.8
(debugger build in `.deps/bochs-2.8/bochs`) the way `xemu/boot.c` does, and
records a per-instruction PC trace that can be diffed against
`./build/xemu --trace-pc=FILE`.

`tools/bochs/trace_compare.py` diffs the two traces and symbolises the first
divergence from `build/kernel/system.map`.

## Quick start

```bash
# 300000-instruction Bochs trace of the Debian vmlinux (default)
python3 tools/bochs/kernel_boot.py

# diff against XEMU
./build/xemu --kernel build/kernel/vmlinux-deb-sym.elf \
    --max-steps=300000 --trace-pc=build/bochs/xemu.pc -b
python3 tools/bochs/trace_compare.py build/bochs/xemu.pc build/bochs/kernel_trace.pc

# smaller / other images
python3 tools/bochs/kernel_boot.py --steps 50000
python3 tools/bochs/kernel_boot.py --stop-at 0xffffffff8111db6b --steps 50000
python3 tools/bochs/kernel_boot.py --flat-image build/tests/t1_basic.bin --steps 5
python3 tools/bochs/trace_compare.py build/bochs/flat_xemu.pc build/bochs/flat_bochs.pc
```

`--stop-at ADDR` stops when RIP reaches ADDR and writes `<out>.stop-at.txt`
with the final register dump and the last 200 PCs (symbolised).

## What is replicated from `xemu/boot.c`

* 512 MiB RAM, zero-filled (XEMU's `xemu_mem` is zero-initialised).
* Every `PT_LOAD` segment copied to its physical address:
  `p_paddr` if non-zero and `< RAM`, else `p_vaddr` normalised by
  `-0xffffffff80000000` when it is a kernel-map address.  `e_entry` is
  normalised the same way (`0xffffffff81000000` -> `0x1000000`).
* `boot_params` (zero page) at `0x10000` with XEMU's exact e820 map,
  cmdline at `0x20000`, optional initramfs high in RAM.
* Long mode with XEMU's identity map (`PML4 0x1000`, `PDPT 0x2000`,
  `PD 0x3000`, 2 MiB pages over RAM), `CR0=0x80000001`, `CR4=0x20`,
  `EFER=0x500`, `CR3=0x1000`.
* `%rip=entry`, `%rsi=0x10000`, `%rsp=0x9f000`, all other GPRs zero,
  `RFLAGS=0x202`, `CS=0x08`, `SS=0x10`.

The boot stub is `tools/bochs/boot_kernel.S`.  The BIOS is **not** run: the
debugger starts the CPU at the stub in real mode, so the ROM window is zero
as well (zero-filled ROM images are used) and the low 1 MiB matches XEMU's.

## Bochs 2.8 quirks worked around

1. **`loadmem` is broken for files > 4 KiB** (its loop never advances the
   address, so only the last 4 KiB chunk is written, at the base address).
   Memory is therefore loaded in page-sized chunks; a content-addressed cache
   keeps them under `build/bochs/chunks/` (regenerated when the ELF changes).
2. **REP instructions are counted per iteration** by the debugger, while XEMU
   executes a whole REP as one instruction.  `clear_bss` alone is an 18M
   iteration `rep stosb`, which would swamp the trace.  The driver steps in
   chunks, detects a REP still running at a chunk boundary, and finishes it
   with tracing disabled via a temporary linear breakpoint on the next
   instruction.  Consecutive duplicate PCs are collapsed, so the resulting
   `.pc` file has XEMU's one-PC-per-instruction granularity
   (`--steps` counts those collapsed instructions).
3. **A halted CPU blocks the debugger's step command**, so the overall
   `--timeout` is the safety net if a guest executes `hlt`.

## `--percpu`: the important caveat

`build/kernel/vmlinux-deb-sym.elf` encodes `.data..percpu` as a `PT_LOAD`
with `p_vaddr = p_paddr = 0`.  XEMU therefore loads it at physical 0, but the
kernel expects it at `__per_cpu_load` (`0xffffffff83042000` -> physical
`0x3042000`): the early GDT page lives at `__per_cpu_load + 0xb000`.

Consequences:

* With XEMU's placement (physical 0) the page at physical `0x304d000` is
  zero, the early GDT descriptor loaded at `0xffffffff810000d4` is a null
  descriptor, and real hardware (Bochs) raises `#GP` on the `retf` at
  `0xffffffff81000158` (vector 13).  XEMU does not fault because its `RETF`
  implementation does not validate the selector, so it continues with a bogus
  CS descriptor.
* The kernel's per-CPU data (`current_task` via `%gs:0x1fb80`, etc.) is also
  copied from `__per_cpu_load`, which is zero in XEMU.  XEMU consequently
  page-faults at `0xffffffff8111db6b` (`mov 0x970(%rax),%eax`, `%rax = 0`)
  after **9755** instructions, which is the start of the oops path seen in
  `xemu/docs/linux-status.md`.

`--percpu` selects the placement:

| mode | behaviour |
| --- | --- |
| `auto` (default) | load at physical 0 *and* at `__per_cpu_load`.  Bochs then boots correctly, the memory image still matches XEMU everywhere XEMU writes, and the PC streams match for the first 9754 instructions. |
| `xemu` | exact XEMU memory image (percpu only at 0).  The trace stops at `0xffffffff81000158` (3111 PCs) with a `#GP`. |
| `kernel` | only at `__per_cpu_load` (equivalent to a correctly linked ELF). |

`build/bochs/vmlinux-paddr-fixed.elf` is a copy of the same ELF with that one
program header's `p_paddr` set to `0x3042000`.  Booting it *in XEMU* fixes the
early crash:

```bash
./build/xemu --kernel build/bochs/vmlinux-paddr-fixed.elf \
    --max-steps=300000 --trace-pc=build/bochs/xemu_fixed.pc -b
# -> stops after 300000 instructions, rip=0xffffffff8152b924, no oops
```

## CPUID limitation

XEMU advertises a synthetic CPU: vendor `"X86-KMH-Core"`, family/model/stepping
`0x663`, SSE2 only, no leaf 7, max extended leaf `0x80000001`.  Bochs cannot be
made bit-identical (long mode requires SEP/MMX/XAPIC bits, and leaves
`0x80000002..8` are always present).  `--cpu xemu` (default) uses `bx_generic`
with a CPUID profile close to XEMU's; `--cpu core2` uses Bochs'
`core2_penryn_t9600`.

With `--cpu xemu` the traces match XEMU (with the percpu fix) up to index
**22291**, then diverge in `get_cpu_cap` because Bochs reports extended leaf
`0x80000008` while XEMU reports `0x80000001` (the kernel then queries
`0x80000007`).  `trace_compare.py --resync W` shows that the streams realign
after such detours; the remaining divergences are all CPUID-feature checks
(`init_scattered_cpuid_features`, `get_cpu_address_sizes`).

## Files produced in `build/bochs/`

| file | contents |
| --- | --- |
| `<out>.pc` | collapsed PC trace, one `0x<hex>` per line (XEMU format) |
| `<out>.raw` | full Bochs debugger log: PC, disassembly, instruction bytes |
| `<out>.regs` | register dump at the end |
| `<out>.meta` | entry, steps, counts, timings, percpu mode |
| `<out>.stop-at.txt` | registers + last 200 PCs when `--stop-at` hits |
| `<out>.bochsrc`, `<out>.bochs.log`, `<out>.stdout` | generated config/logs |

Committed example outputs:

* `build/bochs/kernel_trace.*` – 300000-instruction Bochs oracle (defaults).
* `build/bochs/xemu_300k.pc` – XEMU on the shipped ELF (crashes at 9755).
* `build/bochs/xemu_fixed_300k.pc` – XEMU on the LMA-fixed ELF (boots fine).
* `build/bochs/vmlinux-paddr-fixed.elf` – the fixed ELF.
* `build/bochs/kxemu.pc` – `--percpu xemu`: 3111 PCs then the `#GP`.
* `build/bochs/kstop.*` – `--stop-at 0xffffffff8111db6b` example.
* `build/bochs/flat_bochs.pc`, `flat_xemu.pc` – `t1_basic.bin` sanity check
  (identical).

## Measured speed

* Full 300000-instruction trace: **23.2 s** wall clock (18.5 s of tracing +
  ~4.7 s image load), i.e. **~16k traced instructions/s**; the artificial
  `rep stosb` BSS clear is 18M untraced debugger steps (~5 s).
* Bochs debugger stepping without trace: ~3.3M steps/s.
* Trace on: ~50k steps/s for a simple loop, ~16k/s for paged kernel code
  (disassembly + symbolic-address formatting dominates).
