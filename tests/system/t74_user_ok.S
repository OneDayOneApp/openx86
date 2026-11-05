# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t74_user_ok -- CPL3 access to a U/S=1 page must still succeed.
#
# The CPL3 payload executes from a U=1 code page, reads and writes a U=1 data
# page, then enters the kernel through an int 0x80 gate.  The gate handler
# (CPL0) verifies the frame CS and the data the user wrote, then reports
# 0x74.
#
# This is the positive counterpart to t70: adding U/S enforcement must not
# reject legitimate user pages, and the TLB must not remember a supervisor
# access as user-accessible (or vice versa).
#
# Exit 0x74 on success, 0x7N on a failed check.

.text
.globl _start
_start:
    movq $0x8ff000, %rsp

    # ---- page tables at 0x900000 ---------------------------------------
    movq $0x901007, 0x900000          # PML4[0] -> PDPT (P|RW|U)
    movq $0x902007, 0x901000          # PDPT[0] -> PD   (P|RW|U)
    movq $0x904003, 0x901018          # PDPT[3] -> halt PD
    movq $0x00000083, 0x902000        # PD[0] 0..2 MiB   (U=0)
    movq $0x00200083, 0x902008        # PD[1] 2..4 MiB
    movq $0x00400083, 0x902010        # PD[2] 4..6 MiB
    movq $0x903007, 0x902018          # PD[3] 6..8 MiB -> PT 0x903000 (U=1)
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB (tables, stack)
    movq $0x600007, 0x903000          # 0x600000 user code   U=1 RW
    movq $0x601007, 0x903008          # 0x601000 user stack  U=1 RW
    movq $0x602003, 0x903010          # 0x602000 supervisor target U=0 RW
    movq $0x603007, 0x903018          # 0x603000 user data   U=1 RW
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
    movq %r11, 0x905028               # TSS 0x28, base 0x908000
    movw $0x2f, 0x905F00
    movq $0x905000, 0x905F02
    movq $0x905F00, %rax
    lgdt (%rax)
    movw $0x28, %ax
    ltr %ax
    movq $0x8ff000, 0x908004          # TSS RSP0

    # ---- IDT at 0x906000: v14 -> pf_handler, v0x80 -> user_gate ---------
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

    leaq user_gate(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0xef0000080000, %rdx        # DPL3 interrupt gate
    orq %rdx, %rcx
    movq %rcx, 0x906000 + 0x80*16
    shrq $32, %rax
    movq %rax, 0x906000 + 0x80*16 + 8

    movw $0x0fff, 0x906F00
    movq $0x906000, 0x906F02
    movq $0x906F00, %rax
    lidt (%rax)

    # ---- relocate the CPL3 payload to 0x600000 -------------------------
    leaq user_entry(%rip), %rsi
    movq $0x600000, %rdi
    movl $user_end-user_entry, %ecx
    cld
    rep movsb

    # ---- turn on paging + long mode ------------------------------------
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

    # ---- enter CPL3 ----------------------------------------------------
    pushq $0x23
    pushq $0x601ff0
    pushq $0x202
    pushq $0x1b
    pushq $0x600000
    iretq

# ---- CPL3 payload (position independent, copied to 0x600000) -----------
user_entry:
    movq $0x603000, %rax
    movq (%rax), %rbx                 # read user page: must succeed
    movq $0x7474, %rcx
    movq %rcx, (%rax)                 # write user page: must succeed
    int $0x80                         # back to CPL0
user_end:

# ---- #PF handler: any fault from the user payload is a failure ---------
pf_handler:
    movq $0xf0000000, %r15
    movq 0(%rsp), %rax
    movq %rax, 0x908010               # keep the observed error code
    movq $0x7e, %rax
    movq %rax, (%r15)
1:  jmp 1b

# ---- int 0x80 gate -----------------------------------------------------
user_gate:
    # frame: 0(%rsp)=rip, 8=cs, 16=rflags, 24=rsp, 32=ss
    cmpq $0x1b, 8(%rsp)
    jne fail
    movq $0x603000, %rax
    movabsq $0x7474, %rcx
    cmpq %rcx, (%rax)                 # the user store must have landed
    jne fail
    movq $0xf0000000, %r15
    movq $0x74, %rax
    movq %rax, (%r15)
2:  jmp 2b

fail:
    movq $0xf0000000, %r15
    movq $0x7f, %rax
    movq %rax, (%r15)
3:  jmp 3b
