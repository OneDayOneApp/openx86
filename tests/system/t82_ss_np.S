# t82_ss_np -- segment selector loads must validate the descriptor the way
# Bochs load_seg_reg()/fetch_raw_descriptor() do.
#
# The GDT (limit 0x3f) has a present-bit-clear *writable data* descriptor at
# index 7 (selector 0x38; a 64-bit TSS descriptor spans indices 5 and 6, so
# index 6 must stay zero) and the usual kernel/user/TSS descriptors:
#
#   A1  MOV SS, 0x38   -> #SS(0x38)   (not present; SS wants a writable data
#                                      segment, so the fault is #SS, not #NP)
#   A2  MOV DS, 0x38   -> #NP(0x38)   (not present data segment)
#   A3  MOV DS, 0x320  -> #GP(0x320)  (index 100 past GDTR.LIMIT)
#   A4  MOV SS, 0x08   -> #GP(0x08)   (a code descriptor is not writable data)
#   A5  MOV SS, 0      -> allowed: in long mode a null SS is legal when
#                         RPL == CPL and CPL != 3 (Linux's startup_64 does
#                         exactly this at CPL0)
#   A6  MOV DS, 0      -> allowed: the null selector clears DS
#   B1  MOV SS, 0x08   -> #GP(0x08)   (RPL 0 != CPL 3, from CPL3)
#   B2  MOV SS, 0x23   -> allowed at CPL3
#
# Every selector error code is `selector & 0xfffc`.  Each handler also
# verifies that the faulting load did not take effect (SS/DS unchanged) and
# resumes after the instruction.  Exactly five faults are expected.
#
# Exit 0x82 on success, 0x8N on a failed check.

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

    # ---- GDT at 0x905000, limit 0x37 -----------------------------------
    movq $0, 0x905000                 # 0x00 null
    movabsq $0x00af9a000000ffff, %r11
    movq %r11, 0x905008               # 0x08 kernel code
    movabsq $0x00af92000000ffff, %r11
    movq %r11, 0x905010               # 0x10 kernel data
    movabsq $0x00cffa000000ffff, %r11
    movq %r11, 0x905018               # 0x18 user code
    movabsq $0x00cff2000000ffff, %r11
    movq %r11, 0x905020               # 0x20 user data
    movabsq $0x0000899080000067, %r11
    movq %r11, 0x905028               # 0x28 TSS, base 0x908000, limit 0x67
    movq $0, 0x905030                 # 0x30: high half of the 16-byte TSS desc
    movabsq $0x000012000000ffff, %r11
    movq %r11, 0x905038               # 0x38 writable data, P = 0
    movw $0x3f, 0x905F00
    movq $0x905000, 0x905F02
    movq $0x905F00, %rax
    lgdt (%rax)
    movw $0x28, %ax
    ltr %ax
    movq $0x8ff000, 0x908004          # TSS RSP0

    # ---- IDT at 0x906000 ----------------------------------------------
    SETGATE 11, np_handler, 0x8e
    SETGATE 12, ss_handler, 0x8e
    SETGATE 13, gp_handler, 0x8e
    SETGATE 0x80, done_gate, 0xee     # DPL3: the CPL3 payload reports back
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

    # ---- A1: non-present SS descriptor -> #SS(0x38) --------------------
    movq $0x38, want_err(%rip)
    movq $12, want_vec(%rip)
    leaq after1(%rip), %rax
    movq %rax, resume(%rip)
    movw $0x38, %ax
chk1:
    movw %ax, %ss
after1:

    # ---- A2: non-present DS descriptor -> #NP(0x38) --------------------
    movq $0x38, want_err(%rip)
    movq $11, want_vec(%rip)
    leaq after2(%rip), %rax
    movq %rax, resume(%rip)
    movw $0x38, %ax
chk2:
    movw %ax, %ds
after2:

    # ---- A3: selector past GDTR.LIMIT -> #GP(0x320) --------------------
    movq $0x320, want_err(%rip)
    movq $13, want_vec(%rip)
    leaq after3(%rip), %rax
    movq %rax, resume(%rip)
    movw $0x320, %ax
chk3:
    movw %ax, %ds
after3:

    # ---- A4: a code descriptor is not a writable SS -> #GP(0x08) -------
    movq $0x8, want_err(%rip)
    movq $13, want_vec(%rip)
    leaq after4(%rip), %rax
    movq %rax, resume(%rip)
    movw $0x08, %ax
chk4:
    movw %ax, %ss
after4:

    # ---- A5: null SS is legal at CPL0 in long mode ---------------------
    movq $0, want_err(%rip)
    movq $13, want_vec(%rip)
    leaq after5(%rip), %rax
    movq %rax, resume(%rip)
    xorl %eax, %eax
chk5:
    movw %ax, %ss
after5:
    movw %ss, %ax
    testw %ax, %ax                    # the load must have taken effect
    jne fail_ssnull

    # ---- A6: the null selector clears DS -------------------------------
    movq $0, want_err(%rip)
    movq $13, want_vec(%rip)
    leaq after6(%rip), %rax
    movq %rax, resume(%rip)
    xorl %eax, %eax
chk6:
    movw %ax, %ds
after6:
    movw %ds, %ax
    testw %ax, %ax
    jne fail_dsnull

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
    # ---- B1: SS.RPL 0 != CPL 3 -> #GP(0x08) ---------------------------
    movq $0x8, want_err(%rip)
    movq $13, want_vec(%rip)
    leaq after7(%rip), %rax
    movq %rax, resume(%rip)
    movw $0x08, %ax
chk7:
    movw %ax, %ss
after7:
    # ---- B2: a DPL3 data segment is a valid SS at CPL3 -----------------
    movq $0, want_err(%rip)
    movq $13, want_vec(%rip)
    leaq after8(%rip), %rax
    movq %rax, resume(%rip)
    movw $0x23, %ax
chk8:
    movw %ax, %ss
after8:
    movw %ss, %ax
    cmpw $0x23, %ax
    jne fail_userss
    int $0x80                         # back to CPL0 for the verdict
    jmp fail_ran

# ---- selector-fault handlers ------------------------------------------
ss_handler:
    movq $12, is_vec(%rip)
    jmp sel_common
np_handler:
    movq $11, is_vec(%rip)
    jmp sel_common
gp_handler:
    movq $13, is_vec(%rip)
sel_common:
    # frame: 0(%rsp)=err, 8=rip, 16=cs, 24=rflags, 32=rsp, 40=ss
    movq 0(%rsp), %rax
    cmpq want_err(%rip), %rax         # selector & 0xfffc
    jne fail_err
    movq is_vec(%rip), %rax
    cmpq want_vec(%rip), %rax         # #GP / #NP / #SS as expected
    jne fail_vec
    movq 16(%rsp), %rax
    cmpq $0x8, %rax
    jne sel_from_user
    # Same-level CPL0 fault: the failed load must not have touched SS/DS.
    movw %ss, %ax
    cmpw $0x10, %ax
    jne fail_ss
    movw %ds, %ax
    testw %ax, %ax
    jne fail_ds
    jmp sel_resume
sel_from_user:
    cmpq $0x1b, %rax                  # faulted in the CPL3 payload
    jne fail_cs
sel_resume:
    incq exc_count(%rip)
    movq resume(%rip), %rcx
    movq %rcx, 8(%rsp)
    addq $8, %rsp                     # drop the error code
    iretq

# ---- completion --------------------------------------------------------
done_gate:
    # frame (no error code): 0=rip, 8=cs, 16=rflags, 24=rsp, 32=ss
    cmpq $5, exc_count(%rip)          # A1..A4 and B1, and nothing else
    jne fail_count
    movq $0xf0000000, %r15
    movq $0x82, %rax
    movq %rax, (%r15)
9:  jmp 9b

fail_err:
    movq $0xf0000000, %r15
    movq $0x84, %rax
    movq %rax, (%r15)
8:  jmp 8b
fail_vec:
    movq $0xf0000000, %r15
    movq $0x85, %rax
    movq %rax, (%r15)
7:  jmp 7b
fail_cs:
    movq $0xf0000000, %r15
    movq $0x86, %rax
    movq %rax, (%r15)
6:  jmp 6b
fail_ss:
    movq $0xf0000000, %r15
    movq $0x87, %rax
    movq %rax, (%r15)
5:  jmp 5b
fail_ds:
    movq $0xf0000000, %r15
    movq $0x88, %rax
    movq %rax, (%r15)
4:  jmp 4b
fail_ssnull:
    movq $0xf0000000, %r15
    movq $0x89, %rax
    movq %rax, (%r15)
3:  jmp 3b
fail_dsnull:
    movq $0xf0000000, %r15
    movq $0x8a, %rax
    movq %rax, (%r15)
2:  jmp 2b
fail_userss:
    movq $0xf0000000, %r15
    movq $0x8b, %rax
    movq %rax, (%r15)
1:  jmp 1b
fail_ran:
    movq $0xf0000000, %r15
    movq $0x8c, %rax
    movq %rax, (%r15)
    jmp 1b
fail_count:
    movq $0xf0000000, %r15
    movq $0x8f, %rax
    movq %rax, (%r15)
    jmp 1b

# ---- handler scratch ---------------------------------------------------
.p2align 3
want_err:  .quad 0
want_vec:  .quad 0
is_vec:    .quad 0
resume:    .quad 0
exc_count: .quad 0
