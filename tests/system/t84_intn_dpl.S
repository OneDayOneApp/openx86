# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t84_intn_dpl -- a software interrupt whose IDT gate DPL is lower than CPL
# must raise #GP(0) instead of being delivered.
#
# The CPL3 payload executes `int $0x40` where vector 0x40's gate is DPL0:
# that is #GP(0) (the error code is 0, not a selector) and the frame's RIP is
# the INT instruction itself, because the DPL check faults the instruction.
# `int $0x41` uses a DPL3 gate and must be delivered normally, returning to
# the instruction after it.  The completion gate checks that exactly one of
# each happened and that the DPL0 handler never ran.
#
# Exit 0x84 on success, 0x8N on a failed check.

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
    movq $0x00400087, 0x902010        # PD[2] 4..6 MiB: image + user stack (U=1)
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB: GDT/TSS/IDT (U=0)
    movabsq $0xF0000083, %rax         # halt MMIO 0xF0000000
    movq %rax, 0x904C00

    # ---- GDT + TSS at 0x905000 / 0x908000 ------------------------------
    movabsq $0x00af9a000000ffff, %r11
    movq %r11, 0x905008               # kernel code 0x08
    movabsq $0x00af92000000ffff, %r11
    movq %r11, 0x905010               # kernel data 0x10
    movabsq $0x00cffa000000ffff, %r11
    movq %r11, 0x905018               # user code 0x1b
    movabsq $0x00cff2000000ffff, %r11
    movq %r11, 0x905020               # user data 0x23
    movabsq $0x0000899080000067, %r11
    movq %r11, 0x905028               # TSS 0x28, base 0x908000, limit 0x67
    movw $0x2f, 0x905F00
    movq $0x905000, 0x905F02
    movq $0x905F00, %rax
    lgdt (%rax)
    movw $0x28, %ax
    ltr %ax
    movq $0x8ff000, 0x908004          # TSS RSP0

    # ---- IDT at 0x906000 ----------------------------------------------
    SETGATE 13, gp_handler, 0x8e
    SETGATE 0x40, int40_handler, 0x8e # DPL0: not callable from CPL3
    SETGATE 0x41, int41_handler, 0xee # DPL3: callable from CPL3
    SETGATE 0x80, done_gate, 0xee     # DPL3: completion
    movw $0x0fff, 0x906F00
    movq $0x906000, 0x906F02
    movq $0x906F00, %rax
    lidt (%rax)

    # ---- paging + long mode -------------------------------------------
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

    # ---- enter CPL3 ----------------------------------------------------
    pushq $0x23                       # user SS
    pushq $0x5ff000                   # user RSP
    pushq $0x202
    pushq $0x1b                       # user CS
    leaq user_entry(%rip), %rax
    pushq %rax
    iretq

# ---- CPL3 payload (runs in place: PD[2] is U=1) ------------------------
user_entry:
int40_insn:
    int $0x40                         # DPL0 gate must raise #GP(0)
after_int40:
    int $0x41                         # DPL3 gate must be delivered
after_int41:
    int $0x80                         # completion gate
    jmp fail_ran

# ---- #GP handler for the DPL0 gate ------------------------------------
gp_handler:
    # frame: 0(%rsp)=err, 8=rip, 16=cs, 24=rflags, 32=rsp, 40=ss
    movq 0(%rsp), %rax
    testq %rax, %rax                  # #GP(0), not #GP(selector)
    jne fail_err
    movq 16(%rsp), %rax
    cmpq $0x1b, %rax                  # the faulting CPL is 3
    jne fail_cs
    movq 8(%rsp), %rax
    leaq int40_insn(%rip), %rcx        # RIP = the INT instruction itself
    cmpq %rcx, %rax
    jne fail_rip
    incq gp_count(%rip)
    leaq after_int40(%rip), %rcx
    movq %rcx, 8(%rsp)
    addq $8, %rsp                     # drop the #GP error code
    iretq

# ---- DPL3 gate handler: delivered normally -----------------------------
int41_handler:
    # frame (no error code): 0=rip, 8=cs, 16=rflags, 24=rsp, 32=ss
    cmpq $0x1b, 8(%rsp)
    jne fail_int41_cs
    incq int41_count(%rip)
    iretq

# ---- DPL0 gate handler: reached only if the DPL check is missing ------
int40_handler:
    incq bad_count(%rip)
    iretq

# ---- completion --------------------------------------------------------
done_gate:
    # frame (no error code): 0=rip, 8=cs, 16=rflags, 24=rsp, 32=ss
    cmpq $1, gp_count(%rip)
    jne fail_gp_count
    cmpq $1, int41_count(%rip)
    jne fail_int41_count
    cmpq $0, bad_count(%rip)
    jne fail_bad
    movq $0xf0000000, %r15
    movq $0x84, %rax
    movq %rax, (%r15)
9:  jmp 9b

fail_ran:
    movq $0xf0000000, %r15
    movq $0x85, %rax
    movq %rax, (%r15)
8:  jmp 8b
fail_err:
    movq $0xf0000000, %r15
    movq $0x86, %rax
    movq %rax, (%r15)
7:  jmp 7b
fail_cs:
    movq $0xf0000000, %r15
    movq $0x87, %rax
    movq %rax, (%r15)
6:  jmp 6b
fail_rip:
    movq $0xf0000000, %r15
    movq $0x88, %rax
    movq %rax, (%r15)
5:  jmp 5b
fail_int41_cs:
    movq $0xf0000000, %r15
    movq $0x89, %rax
    movq %rax, (%r15)
4:  jmp 4b
fail_gp_count:
    movq $0xf0000000, %r15
    movq $0x8f, %rax
    movq %rax, (%r15)
3:  jmp 3b
fail_int41_count:
    movq $0xf0000000, %r15
    movq $0x8e, %rax
    movq %rax, (%r15)
2:  jmp 2b
fail_bad:
    movq $0xf0000000, %r15
    movq $0x8d, %rax
    movq %rax, (%r15)
1:  jmp 1b

.p2align 3
gp_count:    .quad 0
int41_count: .quad 0
bad_count:   .quad 0
