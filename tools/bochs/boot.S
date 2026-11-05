/* Minimal long-mode trampoline for Bochs golden-reference runs.
 *
 * The driver loads the payload directly with `loadmem <image> 0x400000`, so
 * this boot sector only has to build page tables and enter long mode:
 *   - zero three page-table pages at 0x1000..0x3fff
 *   - 4-level identity map for 0-16MiB using 2MiB pages
 *   - map VA 0xF0000000 -> physical 0x100000 so the XEMU halt-MMIO store
 *     lands in scratch RAM instead of faulting
 *   - enable PAE + LME + PG, then park at `park`
 *
 * The driver then resumes at `park`, loads a flags stub, and single-steps the
 * payload from an architectural state identical to XEMU's.
 */
    .set BOOT_BASE, 0x7c00

    .code16
    .section .text
    .globl _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp

    /* Zero page tables at 0x1000..0x4fff (PML4, PDPT, two PDs). */
    cld
    movw $0x1000, %di
    xorl %eax, %eax
    movw $8192, %cx
    rep stosw

    /* PML4[0] -> PDPT at 0x2000.
       VA 0xF0000000 has PDPT index 3, so map both PDPT[0] (low 0-16MiB) and
       PDPT[3] (the 0xC0000000-0xFFFFFFFF window) to page directories. */
    movl $0x2003, 0x1000
    movl $0x0000, 0x1004
    movl $0x3003, 0x2000          /* PDPT[0] -> PD at 0x3000 */
    movl $0x0000, 0x2004
    movl $0x4003, 0x2018          /* PDPT[3] -> PD at 0x4000 */
    movl $0x0000, 0x201c

    /* PD[0..7]: identity 2MiB pages for 0-16MiB. */
    movw $0x3000, %di
    xorl %ebx, %ebx
    movw $8, %cx
pd_loop:
    movl %ebx, %eax
    orl $0x83, %eax
    movl %eax, %es:(%di)
    movl $0x0000, %es:4(%di)
    addl $0x200000, %ebx
    addw $8, %di
    loop pd_loop

    /* Second-level PD at 0x4000 covers VA 0xC0000000-0xFFFFFFFF.
       VA 0xF0000000 -> PD index 384; map it to physical 0x200000 so the XEMU
       halt-MMIO store lands in scratch RAM instead of faulting. A 2MiB page
       requires a 2MiB-aligned physical base: 0x100000 would violate the
       reserved bits 12-20 and raise #PF with RSVD=1. */
    movw $(0x4000 + 384*8), %di
    movl $0x200083, %eax
    movl %eax, %es:(%di)
    movl $0x0000, %es:4(%di)

    /* lgdt [BOOT_BASE + gdt_desc]: symbols are 0-based, memory is at 0x7c00. */
    movl $(BOOT_BASE + gdt_desc), %ebx
    lgdtl (%ebx)

    movl $0x1000, %eax
    movl %eax, %cr3
    /* CR4: PAE (bit 5) + OSFXSR (bit 9) + OSXMMEXCPT (bit 10).
       OSFXSR is required before any SSE instruction executes. */
    movl %cr4, %eax
    orl $(0x20 | 0x200 | 0x400), %eax
    movl %eax, %cr4

    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    .byte 0x66, 0xea              /* ljmp 0x08:long_mode_entry */
    .long BOOT_BASE + long_mode_entry
    .word 0x08

    .p2align 3
gdt:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff      /* 64-bit code, DPL0 */
    .quad 0x00af92000000ffff      /* data, DPL0 */
gdt_end:
gdt_desc:
    .word gdt_end - gdt - 1
    .long BOOT_BASE + gdt

    .code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
park:
    jmp park

    .org 510
    .word 0xaa55
