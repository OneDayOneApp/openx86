# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t83_df_ist -- #DF delivery must use TSS.IST1, and a fault while delivering
# another exception must become #DF.
#
# Two IST checks:
#   (a) INT 0x30 from CPL0 through a gate with IST=1.  The stack switch is
#       independent of the privilege change, so the handler must run on
#       TSS.IST1 (0x8fc000..0x8fd000) even though CPL stays 0, and IRET must
#       restore the interrupted RSP.
#   (b) The CPL3 payload reads the unmapped linear address 0x700000.  That
#       #PF must be delivered on TSS.RSP0, which this test points at the
#       unmapped 0x700000, so pushing the #PF frame faults during delivery.
#       The SDM turns a fault detected while delivering an exception into
#       #DF; vector 8's gate uses IST1, so the #DF handler runs on the
#       dedicated stack with error code 0 and the interrupted CPL3 context
#       in the frame.
#
# Exit 0x83 on success, 0x8N on a failed check.

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
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB: GDT/TSS/IDT/IST1 (U=0)
    # PD[3] (6..8 MiB) stays not-present: 0x700000 is the bad RSP0 address.
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
    movq $0x700000, 0x908004          # TSS.RSP0 -> unmapped on purpose
    movq $0x8fd000, 0x908024          # TSS.IST1 top (0x8fc000..0x8fd000)

    # ---- IDT at 0x906000 ----------------------------------------------
    SETGATE 8, df_handler, 0x8e, 1    # #DF on IST1
    SETGATE 14, pf_bad, 0x8e
    SETGATE 0x30, ist_handler, 0x8e, 1
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

    # ---- (a) IST is used even without a privilege change ---------------
    int $0x30
    cmpq $0x8ff000, %rsp              # IRET restored the interrupted RSP
    jne fail_iret_rsp
    cmpq $1, ist_ok(%rip)
    jne fail_ist

    # ---- (b) enter CPL3 and fault with an unusable RSP0 ----------------
    pushq $0x23                       # user SS
    pushq $0x5ff000                   # user RSP
    pushq $0x202
    pushq $0x1b                       # user CS
    leaq user_entry(%rip), %rax
    pushq %rax
    iretq

# ---- CPL3 payload: the read faults, and its delivery cannot use RSP0 ---
user_entry:
    movq $0x700000, %rax
    movq (%rax), %rbx                 # #PF, then #DF
    jmp fail_ran                      # only if neither was delivered

# ---- IST handler for vector 0x30 (same-level, CPL0) --------------------
ist_handler:
    # frame (no error code): 0=rip, 8=cs, 16=rflags, 24=rsp, 32=ss
    cmpq $0x8, 8(%rsp)
    jne fail_cs
    cmpq $0x8ff000, 24(%rsp)          # interrupted RSP
    jne fail_saved_rsp
    cmpq $0x10, 32(%rsp)              # interrupted SS
    jne fail_ss
    movq %rsp, %rax
    cmpq $0x8fd000, %rax
    jae fail_ist_rsp
    cmpq $0x8fc000, %rax
    jb fail_ist_rsp
    movq $1, ist_ok(%rip)
    iretq

# ---- #DF handler on IST1 ----------------------------------------------
df_handler:
    # frame: 0(%rsp)=err, 8=rip, 16=cs, 24=rflags, 32=rsp, 40=ss
    movq 0(%rsp), %rax
    testq %rax, %rax                  # the #DF error code is always 0
    jne fail_df_err
    cmpq $1, ist_ok(%rip)             # part (a) must have passed
    jne fail_ist
    movq %rsp, %rax
    cmpq $0x8fd000, %rax              # RSP lies inside the IST1 stack
    jae fail_df_rsp
    cmpq $0x8fc000, %rax
    jb fail_df_rsp
    movq 16(%rsp), %rax
    cmpq $0x1b, %rax                  # interrupted the CPL3 payload
    jne fail_df_cs
    movq 40(%rsp), %rax
    cmpq $0x23, %rax
    jne fail_df_ss
    movq $0xf0000000, %r15
    movq $0x83, %rax
    movq %rax, (%r15)
9:  jmp 9b

pf_bad:
    movq $0xf0000000, %r15
    movq $0x8f, %rax
    movq %rax, (%r15)
8:  jmp 8b
fail_ran:
    movq $0xf0000000, %r15
    movq $0x8e, %rax
    movq %rax, (%r15)
7:  jmp 7b
fail_iret_rsp:
    movq $0xf0000000, %r15
    movq $0x8d, %rax
    movq %rax, (%r15)
6:  jmp 6b
fail_ist:
    movq $0xf0000000, %r15
    movq $0x8c, %rax
    movq %rax, (%r15)
5:  jmp 5b
fail_ist_rsp:
    movq $0xf0000000, %r15
    movq $0x8b, %rax
    movq %rax, (%r15)
4:  jmp 4b
fail_df_err:
    movq $0xf0000000, %r15
    movq $0x8a, %rax
    movq %rax, (%r15)
3:  jmp 3b
fail_df_rsp:
    movq $0xf0000000, %r15
    movq $0x89, %rax
    movq %rax, (%r15)
2:  jmp 2b
fail_df_cs:
    movq $0xf0000000, %r15
    movq $0x88, %rax
    movq %rax, (%r15)
1:  jmp 1b
fail_df_ss:
    movq $0xf0000000, %r15
    movq $0x87, %rax
    movq %rax, (%r15)
0:  jmp 0b
fail_cs:
    movq $0xf0000000, %r15
    movq $0x86, %rax
    movq %rax, (%r15)
    jmp 0b
fail_saved_rsp:
    movq $0xf0000000, %r15
    movq $0x85, %rax
    movq %rax, (%r15)
    jmp 0b
fail_ss:
    movq $0xf0000000, %r15
    movq $0x84, %rax
    movq %rax, (%r15)
    jmp 0b

.p2align 3
ist_ok: .quad 0
