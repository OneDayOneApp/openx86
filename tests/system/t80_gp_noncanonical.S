# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t80_gp_noncanonical -- a non-canonical linear address must raise #GP(0),
# not #PF, and CR2 must not be written.
#
# With 4-level paging bits 63:48 of a linear address must replicate bit 47.
# XEMU's pager used to translate such an address like any other and report a
# not-present #PF (writing CR2); the SDM requires #GP(0) and leaves CR2 alone.
# The same rule applies to RIP after a branch: `jmp *%rax` with a
# non-canonical target faults at the JMP itself, so the trap frame's RIP is
# the branch instruction (the pager's fetch check is only the backstop for
# RIPs that no branch validated).
#
# Check 1: `movq (%rax), %rcx` with %rax = 0x0000800000000000 (bit 47 = 1,
#          bits 63:48 = 0) -> #GP(0), CR2 still holds the sentinel.
# Check 2: `jmp *%rax` with %rax = 0xffff7fffffffffff (bits 63:48 = 0xffff,
#          bit 47 = 0) -> #GP(0) raised by the JMP.
#
# Exit 0x80 on success, 0x8N on a failed check.

.macro SETGATE vec, handler, attr, ist=0
    leaq \handler(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $\attr, %rdx
    shlq $40, %rdx
    orq %rdx, %rcx
    movq $0x08, %rdx
    shlq $16, %rdx
    orq %rdx, %rcx
.if \ist
    movq $\ist, %rdx
    shlq $32, %rdx
    orq %rdx, %rcx
.endif
    movq %rcx, 0x906000 + \vec*16
    shrq $32, %rax
    movq %rax, 0x906000 + \vec*16 + 8
.endm

.text
.globl _start
_start:
    movq $0x8ff000, %rsp

    # ---- page tables at 0x900000 ---------------------------------------
    movq $0x901007, 0x900000          # PML4[0] -> PDPT (P|RW)
    movq $0x902007, 0x901000          # PDPT[0] -> PD   (P|RW)
    movq $0x904003, 0x901018          # PDPT[3] -> halt PD
    movq $0x00400083, 0x902010        # PD[2] 4..6 MiB   (image)
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB  (stack, tables)
    movabsq $0xF0000083, %rax         # halt MMIO 0xF0000000
    movq %rax, 0x904C00

    # ---- IDT at 0x906000: v13 -> gp_handler, v14 -> pf_bad ------------
    SETGATE 13, gp_handler, 0x8e
    SETGATE 14, pf_bad, 0x8e
    movw $0x0fff, 0x906F00
    movq $0x906000, 0x906F02
    movq $0x906F00, %rax
    lidt (%rax)

    # ---- turn on paging + long mode ------------------------------------
    movq %cr4, %rax
    orq $0x20, %rax                   # PAE
    movq %rax, %cr4
    movq $0x900000, %rax
    movq %rax, %cr3
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax                  # LME
    wrmsr
    movq %cr0, %rax
    movabsq $0x80000001, %rdx         # PG | PE
    orq %rdx, %rax
    movq %rax, %cr0

    # ---- sentinel in CR2: a #PF would overwrite it ---------------------
    movl $0xdeadbeef, %eax
    movq %rax, %cr2

    # ---- check 1: non-canonical data read ------------------------------
    leaq chk1_insn(%rip), %rax
    movq %rax, expected_gp(%rip)
    leaq chk2(%rip), %rax
    movq %rax, resume_gp(%rip)
    movabsq $0x0000800000000000, %rax
chk1_insn:
    movq (%rax), %rcx                 # must #GP(0)
    jmp fail_ran                      # executed only if nothing faulted

    # ---- check 2: non-canonical branch target --------------------------
chk2:
    leaq chk2_jmp(%rip), %rax
    movq %rax, expected_gp(%rip)
    leaq chk3(%rip), %rax
    movq %rax, resume_gp(%rip)
    movabsq $0xffff7fffffffffff, %rax
chk2_jmp:
    jmp *%rax                         # must #GP(0) at the JMP

chk3:
    movq $0xf0000000, %r15
    movq $0x80, %rax
    movq %rax, (%r15)
9:  jmp 9b

# ---- #GP handler -------------------------------------------------------
gp_handler:
    # frame: 0(%rsp)=err, 8=rip, 16=cs, 24=rflags, 32=rsp, 40=ss
    movq 16(%rsp), %rax
    cmpq $0x8, %rax                   # interrupted at CPL0, kernel CS
    jne fail_cs
    movq 0(%rsp), %rax
    testq %rax, %rax                  # #GP(0): error code must be zero
    jne fail_err
    movq %cr2, %rcx
    movl $0xdeadbeef, %eax
    cmpq %rax, %rcx                   # CR2 must not have been written
    jne fail_cr2
    movq 8(%rsp), %rax
    movq expected_gp(%rip), %rcx
    cmpq %rcx, %rax                   # the frame RIP is the faulting insn
    jne fail_rip
    movq resume_gp(%rip), %rcx
    movq %rcx, 8(%rsp)
    addq $8, %rsp                     # drop the #GP error code
    iretq

# ---- a #PF instead of #GP is the bug this test guards against ----------
pf_bad:
    movq $0xf0000000, %r15
    movq $0x8f, %rax
    movq %rax, (%r15)
7:  jmp 7b

fail_ran:
    movq $0xf0000000, %r15
    movq $0x8e, %rax
    movq %rax, (%r15)
6:  jmp 6b
fail_cs:
    movq $0xf0000000, %r15
    movq $0x84, %rax
    movq %rax, (%r15)
5:  jmp 5b
fail_err:
    movq $0xf0000000, %r15
    movq $0x85, %rax
    movq %rax, (%r15)
4:  jmp 4b
fail_cr2:
    movq $0xf0000000, %r15
    movq $0x86, %rax
    movq %rax, (%r15)
3:  jmp 3b
fail_rip:
    movq $0xf0000000, %r15
    movq $0x87, %rax
    movq %rax, (%r15)
2:  jmp 2b

# ---- handler scratch ---------------------------------------------------
.p2align 3
expected_gp: .quad 0
resume_gp:   .quad 0
