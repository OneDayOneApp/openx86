# t75_call_fault -- a faulting stack access must leave the interrupted
# instruction atomically un-executed.
#
# CPL3 executes `call` with RSP in a read-only user page, so the return
# address store raises #PF (P=1, W=1, U=1 -> err 0x7).  Hardware pushes the
# trap frame of the *unmodified* instruction: RIP = the call, RSP = the value
# it had before the call (the push must not have happened).  The handler
# checks exactly that, marks the stack page writable, INVLPGs and IRETs; the
# retried call must then complete and reach user_target.
#
# XEMU used to raise #PF inside the store helper and then let Ops_CALL finish
# `xemu.rip = target`, so the handler RIP was overwritten with the call
# target, the return from the kernel landed in the middle of the callee with
# RSP = the exception stack, and the frame's saved RSP was the half-updated
# one.  In a real Linux userspace this killed every process whose COW stack
# page had to be made writable on a call (busybox ash, fork/exec).
#
# Exit 0x75 on success, 0x7N on the N-th failed check.

.text
.globl _start
_start:
    movq $0x8ff000, %rsp

    # ---- page tables at 0x900000 ---------------------------------------
    movq $0x901007, 0x900000          # PML4[0] -> PDPT (P|RW|U)
    movq $0x902007, 0x901000          # PDPT[0] -> PD   (P|RW|U)
    movq $0x904003, 0x901018          # PDPT[3] -> halt PD
    movq $0x00000083, 0x902000        # PD[0] 0..2 MiB  supervisor
    movq $0x00200083, 0x902008        # PD[1] 2..4 MiB  supervisor
    movq $0x00400087, 0x902010        # PD[2] 4..6 MiB  U=1 (this code)
    movq $0x903007, 0x902018          # PD[3] 6..8 MiB -> PT 0x903000 (U=1)
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB (tables, kernel stack)
    movq $0x600007, 0x903000          # 0x600000 user code   U=1 RW
    movq $0x601005, 0x903008          # 0x601000 user stack  U=1 RO  <-- the trap
    movabsq $0xF0000083, %rax
    movq %rax, 0x904C00

    # ---- GDT + TSS ------------------------------------------------------
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

    # ---- IDT: v14 -> pf_handler, v0x80 -> user_gate ---------------------
    leaq pf_handler(%rip), %rax
    call make_gate                    # rcx = gate word for vector in %rsi
    movq %rcx, 0x906000 + 14*16
    shrq $32, %rax
    movq %rax, 0x906000 + 14*16 + 8

    leaq user_gate(%rip), %rax
    call make_user_gate               # DPL=3 so CPL3 may use int 0x80
    movq %rcx, 0x906000 + 0x80*16
    shrq $32, %rax
    movq %rax, 0x906000 + 0x80*16 + 8

    movw $0x0fff, 0x906F00
    movq $0x906000, 0x906F02
    movq $0x906F00, %rax
    lidt (%rax)

    movq $0, 0x90A000                 # stage counter (TSS lives at 0x908000)
    movq $0, 0x90A008                 # failure code

    # ---- enable paging + WP --------------------------------------------
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
    movabsq $0x80010001, %rdx
    orq %rdx, %rax
    movq %rax, %cr0

    # ---- enter CPL3 at user_start with a read-only user stack ----------
    # iretq frame: [rip][cs][rflags][rsp][ss]
    pushq $0x23                       # ss
    pushq $0x601800                   # rsp (inside the RO page)
    pushq $0x202                      # rflags
    pushq $0x1b                       # cs
    leaq user_start(%rip), %rax
    pushq %rax
    iretq

# ---- CPL3 payload -------------------------------------------------------
user_start:
    call user_target                  # <-- the return-address push faults
after_call:
    jmp .                             # not reached: user_target halts

user_target:
    movl $0x75, %eax
    int $0x80                         # back to CPL0 to report success
    jmp .

# ---- helpers ------------------------------------------------------------
# Build an interrupt gate in %rcx from %rax (handler address).
make_gate:
    movq %rax, %rcx
    andq $0xffff, %rcx                # offset 15:0
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx                    # offset 31:16
    orq %rdx, %rcx
    movq $0x8e0000080000, %rdx        # P|DPL0|int gate, selector 0x08
    orq %rdx, %rcx
    ret

# Same, but DPL=3 (callable from CPL3 via int n).
make_user_gate:
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0xee0000080000, %rdx
    orq %rdx, %rcx
    ret

# ---- CPL0 handlers ------------------------------------------------------
pf_handler:
    incl 0x90A000
    cmpl $1, 0x90A000           # exactly one #PF is expected
    jne fail_reenter

    movq (%rsp), %rax                 # error code
    cmpq $7, %rax                     # P=1, W=1, U=1
    jne fail_err

    leaq user_start(%rip), %rax       # this call is the first instruction
    cmpq %rax, 8(%rsp)                # ... so RIP in the frame must be it
    jne fail_rip

    cmpq $0x601800, 32(%rsp)          # RSP must be the pre-call value
    jne fail_rsp

    movq $0x601007, 0x903008          # make the stack page writable
    movq $0x601000, %rax
    invlpg (%rax)                     # drop the cached read-only translation
    addq $8, %rsp                     # drop the hardware error code
    iretq                             # retry the call

user_gate:
    # int 0x80 from CPL3: [rip][cs][flags][rsp][ss]
    cmpq $0x6017f8, 24(%rsp)          # the retried call must have pushed here
    jne fail_sp
    movq 0x6017f8, %rax
    leaq after_call(%rip), %rcx       # pushed return address = after the call
    cmpq %rcx, %rax
    jne fail_retaddr
    movq $0x75, %r12
    jmp report

fail_reenter: movq $0x01, %r12
    jmp report
fail_err:     movq $0x02, %r12
    jmp report
fail_rip:     movq $0x03, %r12
    jmp report
fail_rsp:     movq $0x04, %r12
    jmp report
fail_sp:      movq $0x05, %r12
    jmp report
fail_retaddr: movq $0x06, %r12
    jmp report

report:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp report
