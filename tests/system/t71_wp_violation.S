# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# t71_wp_violation -- CR0.WP semantics.
#
#   1. With CR0.WP=1, a CPL0 write to a read-only PTE (#PF: P=1, W=1, U=0,
#      RSVD=0) must fault.  The handler validates err/CR2, clears WP and
#      retries the store.
#   2. The retried store must succeed with WP=0 and update the page contents
#      (and the dirty bit).
#   3. Re-enabling WP must make the next store fault again; only then does
#      the test report success.
#
# The old pager ignored WP and faulted on every store to a !RW leaf, so the
# retry looped.  The stage word below catches that: a fault while stage==1
# (i.e. while the retry after clearing WP is in flight) is a failure.
#
# Exit 0x71 on success, 0x7N on a failed check.

.text
.globl _start
_start:
    movq $0x8ff000, %rsp

    # ---- page tables at 0x900000 ---------------------------------------
    movq $0x901003, 0x900000          # PML4[0] -> PDPT
    movq $0x902003, 0x901000          # PDPT[0] -> PD
    movq $0x904003, 0x901018          # PDPT[3] -> halt PD
    movq $0x00000083, 0x902000        # PD[0] 0..2 MiB
    movq $0x00200083, 0x902008        # PD[1] 2..4 MiB
    movq $0x00400083, 0x902010        # PD[2] 4..6 MiB
    movq $0x903003, 0x902018          # PD[3] 6..8 MiB -> PT 0x903000
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB
    movq $0x600001, 0x903000          # 0x600000: P=1, RW=0 (read-only 4K page)
    movabsq $0xF0000083, %rax         # halt MMIO 0xF0000000
    movq %rax, 0x904C00

    # ---- IDT at 0x906000: v14 -> pf_handler -----------------------------
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
    movw $0x0fff, 0x906F00
    movq $0x906000, 0x906F02
    movq $0x906F00, %rax
    lidt (%rax)

    movq $0, 0x908000                 # fault counter
    movq $0, 0x908008                 # stage: 0=first, 1=retry, 2=second

    # ---- turn on paging with CR0.WP=1 ----------------------------------
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
    movabsq $0x80010001, %rdx         # PG | WP | PE
    orq %rdx, %rax
    movq %rax, %cr0

    # ---- store 1: faults (WP=1, page read-only) ------------------------
    # Absolute operands: the handler clobbers registers and the store is
    # retried after iret, so the faulting instruction must not depend on
    # register state.
    movq $0xabcd, 0x600000            # #PF -> handler clears WP and irets

    # execution resumes here after the retried store succeeds
    movq 0x600000, %rbx
    movabsq $0xabcd, %rcx
    cmpq %rcx, %rbx
    jne bad_data

    # ---- store 2: WP=1 again, must fault -------------------------------
    movq %cr0, %rax
    orq $0x10000, %rax                # set WP
    movq %rax, %cr0
    movq $2, 0x908008                 # stage = 2
    movq $0x1234, 0x600000            # #PF -> handler reports 0x71

bad_data:
    movq $0xf0000000, %r15
    movq $0x74, %rax
    movq %rax, (%r15)
3:  jmp 3b

# ---- #PF handler -------------------------------------------------------
pf_handler:
    # frame: 0(%rsp)=err, 8=rip
    movq 0(%rsp), %rax
    cmpq $0x3, %rax                   # P=1, W=1, U=0, RSVD=0
    jne fail_err
    movq %cr2, %rcx
    movabsq $0x600000, %rdx
    cmpq %rdx, %rcx
    jne fail_cr2

    movq 0x908000, %rdx
    incq %rdx
    movq %rdx, 0x908000
    movq 0x908008, %r8

    cmpq $1, %rdx
    jne not_first
    # first fault: clear WP, mark the retry, retry the store
    movq %cr0, %rax
    movabsq $0xFFFFFFFFFFFEFFFF, %rdx
    andq %rdx, %rax                   # clear CR0.WP (bit 16)
    movq %rax, %cr0
    movq $1, 0x908008                 # stage = 1
    addq $8, %rsp                     # drop the hardware error code
    iretq

not_first:
    cmpq $2, %rdx
    jne fail_faults
    cmpq $2, %r8                      # must be the second, post-WP fault
    jne fail_retry
    movq 0x600000, %rax
    movabsq $0xabcd, %rcx
    cmpq %rcx, %rax                   # the WP=0 store must have landed
    jne fail_data
    movq $0xf0000000, %r15
    movq $0x71, %rax
    movq %rax, (%r15)
1:  jmp 1b

fail_err:
    movq $0xf0000000, %r15
    movq $0x75, %rax
    movq %rax, (%r15)
    jmp 1b
fail_cr2:
    movq $0xf0000000, %r15
    movq $0x76, %rax
    movq %rax, (%r15)
    jmp 1b
fail_retry:
    movq $0xf0000000, %r15
    movq $0x77, %rax
    movq %rax, (%r15)
    jmp 1b
fail_faults:
    movq $0xf0000000, %r15
    movq $0x78, %rax
    movq %rax, (%r15)
    jmp 1b
fail_data:
    movq $0xf0000000, %r15
    movq $0x79, %rax
    movq %rax, (%r15)
    jmp 1b
