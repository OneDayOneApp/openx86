# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t73_rsvd -- reserved-bit violations.
#
#   fault 1: a present 4 KiB PTE with bit 51 set must #PF with RSVD=1
#            (err = P=1 | RSVD=1 = 0x9).  The handler clears the bit, then
#            sets bit 63 in the non-leaf PMD that maps the region.
#   fault 2: bit 63 of a non-leaf entry is also reserved, so the retried
#            access must fault with err = 0x9 again.
#
# Exit 0x73 on success, 0x7N on a failed check.

.text
.globl _start
_start:
    movq $0x8ff000, %rsp

    # ---- page tables at 0x900000 ---------------------------------------
    movq $0x901003, 0x900000          # PML4[0] -> PDPT
    movq $0x902003, 0x901000          # PDPT[0] -> PD
    movq $0x904003, 0x901018          # PDPT[3] -> halt PD
    movq $0x00000083, 0x902000        # PD[0] 0..2 MiB
    movq $0x00200083, 0x902008        # PD[1] 2..4 MiB
    movq $0x00400083, 0x902010        # PD[2] 4..6 MiB
    movq $0x903003, 0x902018          # PD[3] 6..8 MiB -> PT 0x903000
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB
    movabsq $0x0008000000600003, %rax # PTE: present|rw + reserved bit 51
    movq %rax, 0x903000
    movabsq $0xF0000083, %rax
    movq %rax, 0x904C00

    # ---- IDT at 0x906000: v14 -> pf_handler -----------------------------
    leaq pf_handler(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x8e0000080000, %rdx
    orq %rdx, %rcx
    movq %rcx, 0x906000 + 14*16
    shrq $32, %rax
    movq %rax, 0x906000 + 14*16 + 8
    movw $0x0fff, 0x906F00
    movq $0x906000, 0x906F02
    movq $0x906F00, %rax
    lidt (%rax)

    movq $0, 0x908000                 # fault counter

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

    # ---- probe: fault 1, retry, fault 2 --------------------------------
    # Absolute operand: the handler clobbers registers and this load is
    # retried after iret.
    movq 0x600000, %rbx               # #PF RSVD (leaf)
    # control never returns here; the second fault halts in the handler
    movq $0xf0000000, %r15
    movq $0x7e, %rax
    movq %rax, (%r15)
4:  jmp 4b

# ---- #PF handler -------------------------------------------------------
pf_handler:
    movq 0(%rsp), %rax
    cmpq $0x9, %rax                   # P=1 | RSVD=1
    jne fail_err
    movq %cr2, %rcx
    movabsq $0x600000, %rdx
    cmpq %rdx, %rcx
    jne fail_cr2

    movq 0x908000, %rdx
    incq %rdx
    movq %rdx, 0x908000
    cmpq $1, %rdx
    jne second

    # first fault: clear PTE bit 51, then set PMD bit 63 (non-leaf RSVD)
    movq $0x600003, 0x903000
    movq 0x902018, %rax
    movabsq $0x8000000000000000, %rdx
    orq %rdx, %rax
    movq %rax, 0x902018
    movq $0x600000, %rax
    invlpg (%rax)
    addq $8, %rsp                     # drop the error code
    iretq

second:
    cmpq $2, %rdx
    jne fail_count
    movq $0xf0000000, %r15
    movq $0x73, %rax
    movq %rax, (%r15)
1:  jmp 1b

fail_err:
    movq $0xf0000000, %r15
    movq $0x75, %rax
    movq %rax, (%r15)
    jmp 1b
fail_cr2:
    movq $0xf0000000, %r15
    movq $0x76, %rax
    movq %rax, (%r15)
    jmp 1b
fail_count:
    movq $0xf0000000, %r15
    movq $0x77, %rax
    movq %rax, (%r15)
    jmp 1b
