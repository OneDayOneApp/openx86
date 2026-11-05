# t79_sysenter -- SYSENTER/SYSEXIT (CPUID.01h:EDX.SEP) must actually work.
#
# t78 pins the SEP bit in CPUID leaf 1; this test proves the claim, so the
# "CPUID must reflect the implementation" rule in docs/PRD.md holds in both
# directions.
#
#   WRMSR 0x174 = 0x08 (IA32_SYSENTER_CS), 0x175 = ESP, 0x176 = EIP
#   SYSENTER   -> CS = 0x08, SS = 0x10, CPL 0, RIP = EIP, RSP = ESP, IF = 0
#   SYSEXIT    -> CS = 0x1b, SS = 0x23, CPL 3, RIP = RDX, RSP = RCX
#
# Exit 0x79 on success, 0x7N on the N-th failed check.

.text
.globl _start
_start:
    movq $0x9ff000, %rsp

    # ---- page tables at 0x900000 ----------------------------------------
    movq $0x901007, 0x900000          # PML4[0] -> PDPT (P|RW|U)
    movq $0x902007, 0x901000          # PDPT[0] -> PD   (P|RW|U)
    movq $0x904007, 0x901018          # PDPT[3] -> halt PD (U=1)
    movq $0x00000083, 0x902000        # 0..2 MiB   supervisor
    movq $0x00200083, 0x902008        # 2..4 MiB   supervisor
    movq $0x00400087, 0x902010        # 4..6 MiB   U=1 (this code)
    movq $0x00600087, 0x902018        # 6..8 MiB   U=1 (kernel/user stacks)
    movq $0x00800083, 0x902020        # 8..10 MiB  (GDT, page tables)
    movabsq $0xF0000087, %rax         # halt MMIO, U=1 so CPL3 can report
    movq %rax, 0x904C00

    # ---- GDT at 0x905000 ------------------------------------------------
    movabsq $0x00af9a000000ffff, %r11
    movq %r11, 0x905008               # 0x08 kernel code
    movabsq $0x00af92000000ffff, %r11
    movq %r11, 0x905010               # 0x10 kernel data (SYSENTER SS)
    movabsq $0x00cffa000000ffff, %r11
    movq %r11, 0x905018               # 0x18 user code (SYSEXIT CS = 0x1b)
    movabsq $0x00cff2000000ffff, %r11
    movq %r11, 0x905020               # 0x20 user data (SYSEXIT SS = 0x23)
    movw $0x27, 0x905F00
    movq $0x905000, 0x905F02
    movq $0x905F00, %rax
    lgdt (%rax)

    # ---- paging + PE + CR4.SEP ------------------------------------------
    movq %cr4, %rax
    orq $0x20 | 0x800, %rax           # PAE | SEP
    movq %rax, %cr4
    movq $0x900000, %rax
    movq %rax, %cr3
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr
    movq %cr0, %rax
    movabsq $0x80010001, %rdx
    orq %rdx, %rax
    movq %rax, %cr0

    # ---- load the SYSENTER MSRs -----------------------------------------
    movl $0x174, %ecx
    xorl %eax, %eax
    movl $0x08, %eax
    xorl %edx, %edx
    wrmsr                             # IA32_SYSENTER_CS = 0x08
    movl $0x175, %ecx
    movl $0x7ff000, %eax
    xorl %edx, %edx
    wrmsr                             # IA32_SYSENTER_ESP
    movl $0x176, %ecx
    leaq sysenter_target(%rip), %rax
    movq %rax, %rdx
    shrq $32, %rdx
    wrmsr                             # IA32_SYSENTER_EIP

    sti                               # IF must be cleared by SYSENTER
    sysenter

# ---- kernel side: reached with CS=0x08 SS=0x10 CPL0 RSP=0x7ff000 --------
sysenter_target:
    movw %cs, %ax
    cmpw $0x08, %ax
    jne bad1
    movw %ss, %ax
    cmpw $0x10, %ax
    jne bad2
    movabsq $0x7ff000, %rax
    cmpq %rax, %rsp
    jne bad3
    pushfq
    popq %rax
    testq $0x200, %rax                # IF must be clear
    jnz bad4

    # ---- SYSEXIT back to CPL3 ------------------------------------------
    movabsq $0x7fe000, %rcx           # RSP for the user side
    leaq user_target(%rip), %rdx      # RIP for the user side
    sysexit

# ---- user side: CS=0x1b SS=0x23 CPL3 RSP=0x7fe000 -----------------------
user_target:
    movw %cs, %ax
    cmpw $0x1b, %ax
    jne bad5
    movw %ss, %ax
    cmpw $0x23, %ax
    jne bad6
    movabsq $0x7fe000, %rax
    cmpq %rax, %rsp
    jne bad7
    movq $0x79, %r12
    jmp finish
bad1:  movq $0x01, %r12
    jmp finish
bad2:  movq $0x02, %r12
    jmp finish
bad3:  movq $0x03, %r12
    jmp finish
bad4:  movq $0x04, %r12
    jmp finish
bad5:  movq $0x05, %r12
    jmp finish
bad6:  movq $0x06, %r12
    jmp finish
bad7:  movq $0x07, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish
