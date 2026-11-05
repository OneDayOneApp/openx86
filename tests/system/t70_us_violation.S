# t70_us_violation -- a CPL3 read of a supervisor (U/S=0) page must raise
# #PF with error code P=1, W=0, U=1, RSVD=0, I/D=0 = 0x5.
#
# The page tables deliberately use Linux's layout: the U/S bit is set on the
# upper levels for the user range and cleared only in the leaf PTE of the
# supervisor page.
#
# The #PF handler runs at CPL0 (TSS RSP0), validates the hardware error code
# and CR2, then stores 0x70 to the halt MMIO.
#
# Exit 0x70 on success, 0x7N on the N-th failed check.

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
    movq %r11, 0x905028               # TSS 0x28, base 0x908000, limit 0x67
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
    movq $0x8e0000080000, %rdx        # type_attr=0x8e, sel=0x08
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
    movq $0xef0000080000, %rdx        # type_attr=0xef, sel=0x08 (DPL3)
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

    # ---- enter CPL3 and read the supervisor page -----------------------
    pushq $0x23                       # user SS
    pushq $0x601ff0                   # user RSP
    pushq $0x202
    pushq $0x1b                       # user CS
    pushq $0x600000
    iretq

# ---- CPL3 payload (position independent, copied to 0x600000) -----------
user_entry:
    movq $0x602000, %rax
    movq (%rax), %rbx                 # must #PF
    int $0x80                         # not reached on success
user_end:

# ---- #PF handler -------------------------------------------------------
pf_handler:
    # frame: 0(%rsp)=err, 8=rip, 16=cs, 24=rflags, 32=rsp, 40=ss
    cmpq $0x1b, 16(%rsp)
    jne fail_cs
    movq 0(%rsp), %rax
    cmpq $0x5, %rax                   # P=1, W=0, U=1, RSVD=0
    jne fail_err
    movq %cr2, %rcx
    movabsq $0x602000, %rdx
    cmpq %rdx, %rcx
    jne fail_cr2
    movq $0xf0000000, %r15
    movq $0x70, %rax
    movq %rax, (%r15)
1:  jmp 1b

fail_err:
    movq $0xf0000000, %r15
    movq $0x71, %rax
    movq %rax, (%r15)
    jmp 1b
fail_cr2:
    movq $0xf0000000, %r15
    movq $0x72, %rax
    movq %rax, (%r15)
    jmp 1b
fail_cs:
    movq $0xf0000000, %r15
    movq $0x73, %rax
    movq %rax, (%r15)
    jmp 1b

# ---- int 0x80 gate: reached only if the supervisor read wrongly worked --
user_gate:
    movq $0xf0000000, %r15
    movq $0x7f, %rax
    movq %rax, (%r15)
2:  jmp 2b
