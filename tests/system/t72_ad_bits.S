# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t72_ad_bits -- the pager must set the accessed bit (bit 5) on every
# successful access and the dirty bit (bit 6) on every successful write, for
# 4 KiB PTEs, 2 MiB PMD pages and 1 GiB PUD pages.
#
# Each check first reads the page (which populates the TLB) and then writes
# it.  The write therefore hits the TLB, exercising the "A/D must stay
# coherent with a cached translation" path: the dirty bit must still be
# written back to the page-table entry.
#
# Exit 0x72 on success, 0x7N on a failed check.

.text
.globl _start
_start:
    movq $0x8ff000, %rsp

    # ---- page tables at 0x900000 ---------------------------------------
    movq $0x901003, 0x900000          # PML4[0] -> PDPT
    movq $0x902003, 0x901000          # PDPT[0] -> PD
    movq $0x00000083, 0x901008        # PDPT[1]: 1 GiB page -> physical 0
    movq $0x904003, 0x901018          # PDPT[3] -> halt PD
    movq $0x00000083, 0x902000        # PD[0] 0..2 MiB
    movq $0x00200083, 0x902008        # PD[1] 2..4 MiB (2 MiB A/D test)
    movq $0x00400083, 0x902010        # PD[2] 4..6 MiB
    movq $0x903003, 0x902018          # PD[3] 6..8 MiB -> PT 0x903000
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB
    movq $0x600003, 0x903000          # 0x600000: 4 KiB page (A/D test)
    movabsq $0xF0000083, %rax         # halt MMIO 0xF0000000
    movq %rax, 0x904C00

    # ---- turn on paging -------------------------------------------------
    movq %cr4, %rax
    orq $0x20, %rax
    movq %rax, %cr4
    movq $0x900000, %rax
    movq %rax, %cr3
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr
    movq %cr0, %rax
    movabsq $0x80000001, %rdx
    orq %rdx, %rax
    movq %rax, %cr0

    # ---- 4 KiB PTE: read sets A, write sets D ---------------------------
    movq $0x600000, %rax
    movq (%rax), %rbx                 # read: A = 1, D = 0
    movq 0x903000, %rcx
    testq $0x20, %rcx
    jz bad_a4k
    testq $0x40, %rcx
    jnz bad_d4k
    movq $0x1111, (%rax)              # write: TLB hit, D = 1
    movq 0x903000, %rcx
    testq $0x20, %rcx
    jz bad_a4k2
    testq $0x40, %rcx
    jz bad_d4k2

    # ---- 2 MiB PMD page: same protocol via the PMD entry ---------------
    movq $0x300000, %rax
    movq (%rax), %rbx                 # read
    movq 0x902008, %rcx
    testq $0x20, %rcx
    jz bad_a2m
    testq $0x40, %rcx
    jnz bad_d2m
    movq $0x2222, (%rax)              # write: TLB hit, D = 1
    movq 0x902008, %rcx
    testq $0x20, %rcx
    jz bad_a2m2
    testq $0x40, %rcx
    jz bad_d2m2

    # ---- 1 GiB PUD page: VA 0x40000000 -> physical 0 -------------------
    movq $0x40080000, %rax            # -> physical 0x80000
    movq (%rax), %rbx                 # read
    movq 0x901008, %rcx
    testq $0x20, %rcx
    jz bad_a1g
    testq $0x40, %rcx
    jnz bad_d1g
    movq $0x3333, (%rax)              # write: TLB hit, D = 1
    movq 0x901008, %rcx
    testq $0x20, %rcx
    jz bad_a1g2
    testq $0x40, %rcx
    jz bad_d1g2

    movq $0xf0000000, %r15
    movq $0x72, %rax
    movq %rax, (%r15)
1:  jmp 1b

bad_a4k:  movq $0x73, %r12
    jmp fail
bad_d4k:  movq $0x74, %r12
    jmp fail
bad_a4k2: movq $0x75, %r12
    jmp fail
bad_d4k2: movq $0x76, %r12
    jmp fail
bad_a2m:  movq $0x77, %r12
    jmp fail
bad_d2m:  movq $0x78, %r12
    jmp fail
bad_a2m2: movq $0x79, %r12
    jmp fail
bad_d2m2: movq $0x7a, %r12
    jmp fail
bad_a1g:  movq $0x7b, %r12
    jmp fail
bad_d1g:  movq $0x7c, %r12
    jmp fail
bad_a1g2: movq $0x7d, %r12
    jmp fail
bad_d1g2: movq $0x7e, %r12
    jmp fail

fail:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
2:  jmp 2b
