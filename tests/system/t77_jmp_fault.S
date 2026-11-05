# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t77_jmp_fault -- an indirect JMP whose memory operand faults must keep the
# #PF handler's RIP, not overwrite it with the 0 returned by the faulting load.
#
# The user payload executes `jmp *0x603000` while that data page is not
# present.  The load raises #PF (P=0, W=0, U=1 = error 0x4) and
# xemu_raise_exc() installs the vector-14 handler in RIP.  A buggy decoder
# then assigned the load result (0) to RIP, so the CPU fetched linear address
# 0 in *kernel* mode with the user GS base still loaded.  On real hardware the
# handler runs first, maps the page, and the retried JMP succeeds.
#
# The handler checks the error code, CR2, CS and faulting RIP, installs the
# missing PTE, stores the jump target (RIP + instruction length) into the
# page, invlpg's and iretq's.  The retried JMP must reach user_ok.
#
# Exit 0x77 on success, 0x7N on a failed check.

.text
.globl _start
_start:
    movq $0x8ff000, %rsp

    # ---- page tables at 0x900000 ---------------------------------------
    movq $0x901007, 0x900000          # PML4[0] -> PDPT (P|RW|U)
    movq $0x902007, 0x901000          # PDPT[0] -> PD   (P|RW|U)
    movq $0x904007, 0x901018          # PDPT[3] -> halt PD (U=1)
    movq $0x00000083, 0x902000        # PD[0] 0..2 MiB   (U=0)
    movq $0x00200083, 0x902008        # PD[1] 2..4 MiB
    movq $0x00400083, 0x902010        # PD[2] 4..6 MiB
    movq $0x903007, 0x902018          # PD[3] 6..8 MiB -> PT 0x903000 (U=1)
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB (tables, stack)
    movq $0x600007, 0x903000          # 0x600000 user code   U=1 RW
    movq $0x601007, 0x903008          # 0x601000 user stack  U=1 RW
    movq $0,        0x903018          # 0x603000 data page: NOT PRESENT
    movabsq $0xF0000087, %rax         # halt MMIO 0xF0000000 (U=1)
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

    # ---- IDT at 0x906000: v14 -> pf_handler -----------------------------
    leaq pf_handler(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x8e0000080000, %rdx        # type_attr=0x8e, sel=0x08
    orq %rdx, %rcx
    movq %rcx, 0x906000 + 14*16
    shrq $32, %rax
    movq %rax, 0x906000 + 14*16 + 8
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
    pushq $0x601ff0                   # user RSP
    pushq $0x202
    pushq $0x1b                       # user CS
    pushq $0x600000
    iretq

# ---- CPL3 payload (position independent, copied to 0x600000) -----------
user_entry:
    jmp *0x603000                     # data page not present: must #PF
user_ok:
    movq $0xf0000000, %r15
    movq $0x77, %rax
    movq %rax, (%r15)
user_end:

# ---- #PF handler -------------------------------------------------------
pf_handler:
    # frame: 0(%rsp)=err, 8=rip, 16=cs, 24=rflags, 32=rsp, 40=ss
    cmpq $0x1b, 16(%rsp)
    jne fail_cs
    movq 0(%rsp), %rax
    cmpq $0x4, %rax                   # P=0, W=0, U=1
    jne fail_err
    movq %cr2, %rcx
    movabsq $0x603000, %rdx
    cmpq %rdx, %rcx
    jne fail_cr2
    movq 8(%rsp), %rcx                # faulting RIP (the JMP itself)
    movabsq $0x600000, %rdx
    cmpq %rdx, %rcx                   # payload starts at 0x600000
    jne fail_rip
    cmpw $0x24ff, (%rcx)              # ff 24 ... = jmp *m64, no REX
    jne fail_rip

    # map the page and store the jump target: RIP + length of `jmp *m64`
    movq $0x603007, 0x903018
    addq $7, %rcx                     # user_ok follows the 7-byte JMP
    movq %rcx, 0x603000               # CPL0: write through the fresh mapping
    movq $0x603000, %rax
    invlpg (%rax)

    addq $8, %rsp                     # drop the error code
    iretq                             # retry the JMP

fail_cs:
    movq $0xf0000000, %r15
    movq $0x79, %rax
    movq %rax, (%r15)
    jmp 9f
fail_err:
    movq $0xf0000000, %r15
    movq $0x7a, %rax
    movq %rax, (%r15)
    jmp 9f
fail_cr2:
    movq $0xf0000000, %r15
    movq $0x7b, %rax
    movq %rax, (%r15)
    jmp 9f
fail_rip:
    movq $0xf0000000, %r15
    movq $0x7c, %rax
    movq %rax, (%r15)

9:  jmp 9b
