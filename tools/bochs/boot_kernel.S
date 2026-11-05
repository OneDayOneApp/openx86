# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
/* Long-mode launch stub for tools/bochs/kernel_boot.py.
 *
 * Unlike boot.S this stub is NOT reached through the BIOS.  The Bochs debugger
 * is at its initial prompt (CPU reset state) before the first instruction is
 * fetched, so the driver simply does
 *
 *      loadmem "boot_kernel.bin" 0x40000
 *      set cs = 0x4000            ; real-mode selector -> base 0x40000
 *      set rip = 0
 *      pb <kernel entry>
 *      c
 *
 * The stub then builds the same page tables xemu's setup_long_mode() builds
 * (xemu/boot.c), switches to long mode, loads the architectural state XEMU
 * hands to the kernel, and jumps to the ELF entry point.
 *
 * Memory layout (matches XEMU after xemu_boot_kernel_initrd(), except that
 * kernel_boot.py --percpu auto also loads .data..percpu at __per_cpu_load):
 *   0x0000..0x34fff .data..percpu, loaded from the ELF by loadmem
 *   0x1000..0x3fff  PML4 / PDPT / PD, exactly as xemu writes them
 *   0x10000         boot_params (zero page), written by loadmem
 *   0x20000         command line, written by loadmem
 *   0x40000..       this stub (the only XEMU-visible deviation; see README)
 *   0x9f000         initial %rsp
 *   0x1000000       vmlinux (ELF p_paddr - 0xffffffff80000000)
 *
 * Segment/register state at the jump matches xemu/main.c:
 *   %cs = 0x08, %ss = 0x10, all other selectors 0x10,
 *   %rsi = 0x10000, %rsp = 0x9f000, all other GPRs = 0,
 *   RFLAGS = 0x202, CR0 = 0x80000001, CR4 = 0x20, EFER = 0x500, CR3 = 0x1000.
 */
    .set STUB_BASE,   0x40000
    .set BOOT_PARAMS, 0x10000

    .code16
    .section .text
    .globl _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* No stack is used before long mode: the 16-bit part only touches page
       table memory, so %sp is left alone until the 64-bit entry. */

    /* A20 via the fast gate (port 0x92).  Bochs enables A20 at reset, but the
       BIOS is skipped here, so do it explicitly anyway. */
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    /* Zero PML4 (0x1000), PDPT (0x2000), PD (0x3000..0x3fff): 12 KiB.
       xemu's setup_long_mode() memsets exactly this range. */
    cld
    movw $0x1000, %di
    xorl %eax, %eax
    movw $6144, %cx
    rep stosw

    /* PML4[0] -> PDPT, PDPT[0] -> PD. */
    movl $0x2003, 0x1000
    movl $0x0000, 0x1004
    movl $0x3003, 0x2000
    movl $0x0000, 0x2004

    /* PD[0..255] = identity 2 MiB pages covering all 512 MiB of RAM
       (xemu: pages = XEMU_MEM_SIZE >> 21, capped at 512 entries). */
    movw $0x3000, %di
    xorl %ebx, %ebx
    movw $256, %cx
1:
    movl %ebx, %eax
    orl $0x83, %eax
    movl %eax, %es:(%di)
    movl $0x0000, %es:4(%di)
    addl $0x200000, %ebx
    addw $8, %di
    loop 1b

    /* Load the GDT through DS = 0x4000 (base 0x40000).  In real mode the
       segment limit is 64 KiB, and this stub lives at 0x40000, so an absolute
       DS:0x400c0 access would trip "read beyond limit".  Using DS base 0x40000
       plus the section-relative offset keeps the effective offset < 64 KiB;
       the GDTR base field itself is still the full linear address. */
    movw $0x4000, %ax
    movw %ax, %ds
    movw $gdt_desc, %bx
    lgdtl (%bx)

    movl $0x1000, %eax
    movl %eax, %cr3

    /* CR4 = PAE only, matching xemu.cr4 = 0x20. */
    movl $0x20, %eax
    movl %eax, %cr4

    /* EFER.LME = 1; LMA is set by hardware when CR0.PG is set. */
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    /* CR0 = PG | PE, matching xemu.cr0 = 0x80000001. */
    movl $0x80000001, %eax
    movl %eax, %cr0

    .byte 0x66, 0xea              /* ljmp 0x08:long_mode_entry */
    .long STUB_BASE + long_mode_entry
    .word 0x08

    .p2align 3
gdt:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff      /* 64-bit code, DPL0 */
    .quad 0x00af92000000ffff      /* data, DPL0 */
gdt_end:
gdt_desc:
    .word gdt_end - gdt - 1
    .long STUB_BASE + gdt

    .code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    /* XEMU hands the kernel a zeroed register file (except %rsi and %rsp). */
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %ebp, %ebp
    xorl %edi, %edi
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d
    movl $BOOT_PARAMS, %esi
    movl stack_ptr(%rip), %esp

    pushq $0x202
    popfq

    /* Jump without clobbering any GPR: the targets live in memory.  The
       driver patches the magic constants below with the ELF entry point and
       the initial stack pointer. */
    jmp *entry_ptr(%rip)

    .p2align 3
entry_ptr:
    .quad 0xdeadbeefcafef00d
stack_ptr:
    .quad 0x1122334455667788
