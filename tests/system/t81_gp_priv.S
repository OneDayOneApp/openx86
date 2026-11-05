# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t81_gp_priv -- privileged instructions executed at CPL3 must raise #GP(0)
# instead of silently executing.
#
# The CPL3 payload runs in place (the 4..6 MiB page is U=1) and executes
# CLI, HLT, OUT, WRMSR and MOV to CR0 in turn.  Each one must fault with
# the error code must be 0, and the trap frame's RIP must point at the
# faulting instruction;
# the CPL0 handler checks the frame, counts the faults and resumes the user
# code after the instruction.  WRMSR targets IA32_FS_BASE (0xC0000100) and
# the CR0 write carries a reserved bit (0x40) so a silent execution is
# visible afterwards: the completion interrupt then sees a non-zero FS base
# or a changed CR0 and fails.
#
# Exit 0x81 on success, 0x8N on a failed check.

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
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB: GDT/TSS/IDT/kstack (U=0)
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
    SETGATE 0x80, done_gate, 0xee     # DPL3: the CPL3 payload may call it
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
    movq %rax, expected_cr0(%rip)     # the CPL3 MOV CR0 must not change this
    movq %rax, %rbx
    orq $0x40, %rbx                   # reserved bit: visible if it lands

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
priv_cli:
    cli                               # must #GP(0)
after_cli:
priv_hlt:
    hlt                               # must #GP(0)
after_hlt:
    movl $1, %eax
priv_out:
    outb %al, $0x80                   # must #GP(0) (IOPL = 0)
after_out:
    movl $0xc0000100, %ecx            # IA32_FS_BASE
    movl $0xdead, %eax
    xorl %edx, %edx
priv_wrmsr:
    wrmsr                             # must #GP(0)
after_wrmsr:
priv_movcr:
    movq %rbx, %cr0                   # must #GP(0)
after_movcr:
    int $0x80                         # back to CPL0 for the verdict

# ---- #GP handler -------------------------------------------------------
gp_handler:
    # frame: 0(%rsp)=err, 8=rip, 16=cs, 24=rflags, 32=rsp, 40=ss
    movq 0(%rsp), %rax
    testq %rax, %rax                  # #GP(0)
    jne fail_err
    movq 16(%rsp), %rax
    cmpq $0x1b, %rax                  # faulted in the CPL3 payload
    jne fail_cs
    movq 8(%rsp), %rax                # frame RIP = the faulting instruction
    /* Addresses are RIP-relative: the image is loaded at 0x400000 but the
       object is linked at 0, so absolute immediates would be section offsets. */
    leaq priv_cli(%rip), %rcx
    cmpq %rcx, %rax
    je 1f
    leaq priv_hlt(%rip), %rcx
    cmpq %rcx, %rax
    je 2f
    leaq priv_out(%rip), %rcx
    cmpq %rcx, %rax
    je 3f
    leaq priv_wrmsr(%rip), %rcx
    cmpq %rcx, %rax
    je 4f
    leaq priv_movcr(%rip), %rcx
    cmpq %rcx, %rax
    je 5f
    jmp fail_rip
1:  leaq after_cli(%rip), %rcx
    jmp gp_resume
2:  leaq after_hlt(%rip), %rcx
    jmp gp_resume
3:  leaq after_out(%rip), %rcx
    jmp gp_resume
4:  leaq after_wrmsr(%rip), %rcx
    jmp gp_resume
5:  leaq after_movcr(%rip), %rcx
gp_resume:
    movq %rcx, 8(%rsp)
    incq gp_count(%rip)
    addq $8, %rsp                     # drop the #GP error code
    iretq

# ---- completion interrupt: the CPL3 instructions must have done nothing -
done_gate:
    # frame (no error code): 0=rip, 8=cs, 16=rflags, 24=rsp, 32=ss
    cmpq $5, gp_count(%rip)
    jne fail_count
    movq %cr0, %rax
    movq expected_cr0(%rip), %rcx
    cmpq %rcx, %rax                   # CR0 unchanged (reserved bit absent)
    jne fail_cr0
    movl $0xc0000100, %ecx
    rdmsr
    shlq $32, %rdx
    orq %rdx, %rax
    testq %rax, %rax                  # IA32_FS_BASE must still be 0
    jne fail_msr
    movq $0xf0000000, %r15
    movq $0x81, %rax
    movq %rax, (%r15)
9:  jmp 9b

fail_err:
    movq $0xf0000000, %r15
    movq $0x82, %rax
    movq %rax, (%r15)
8:  jmp 8b
fail_cs:
    movq $0xf0000000, %r15
    movq $0x83, %rax
    movq %rax, (%r15)
7:  jmp 7b
fail_rip:
    movq $0xf0000000, %r15
    movq $0x84, %rax
    movq %rax, (%r15)
6:  jmp 6b
fail_count:
    movq $0xf0000000, %r15
    movq $0x8f, %rax
    movq %rax, (%r15)
5:  jmp 5b
fail_cr0:
    movq $0xf0000000, %r15
    movq $0x8e, %rax
    movq %rax, (%r15)
4:  jmp 4b
fail_msr:
    movq $0xf0000000, %r15
    movq $0x8d, %rax
    movq %rax, (%r15)
3:  jmp 3b

# ---- kernel scratch ----------------------------------------------------
.p2align 3
expected_cr0: .quad 0
gp_count:     .quad 0
