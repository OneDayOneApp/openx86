# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# SGDT/SIDT round-trip: a 64-bit pseudo-descriptor is 10 bytes (16-bit limit
# followed by a 64-bit base).  XEMU used to classify 0F AE as an x87 escape
# and leave 0F 01 /0,/1 UNDEF, so Linux's oops header printed garbage
# ("IDT: 0x81187f9cffff8880 (limit=0x19)") and the base lost its high half.
#
# Layout in the scratch page:
#   0x100 original GDTR   0x120 original IDTR
#   0x140 test GDTR       0x160 test IDTR
#   0x180 GDTR read back  0x1a0 IDTR read back
#
# Result goes to the halt MMIO; 0x64 means every check passed.

.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %r14

    # Snapshot the loader's descriptors so we can restore them at the end.
    sgdt 0x100(%r14)
    sidt 0x120(%r14)

    # --- SGDT round-trip (base beyond 32 bits) --------------------------
    movabsq $0xffff888012345600, %rax
    movw $0x7f, 0x140(%r14)       # pseudo-descriptor: limit first ...
    movq %rax, 0x142(%r14)        # ... then the 64-bit base
    lgdt 0x140(%r14)
    sgdt 0x180(%r14)
    movzwl 0x180(%r14), %eax
    cmpl $0x7f, %eax
    jne bad1
    movq 0x182(%r14), %rax
    movabsq $0xffff888012345600, %rcx
    cmpq %rcx, %rax
    jne bad2

    # --- SIDT round-trip ------------------------------------------------
    movabsq $0xffffff0000abcdef, %rax
    movw $0xfff, 0x160(%r14)
    movq %rax, 0x162(%r14)
    lidt 0x160(%r14)
    sidt 0x1a0(%r14)
    movzwl 0x1a0(%r14), %eax
    cmpl $0xfff, %eax
    jne bad3
    movq 0x1a2(%r14), %rax
    movabsq $0xffffff0000abcdef, %rcx
    cmpq %rcx, %rax
    jne bad4

    # --- LMSW/SMSW round-trip through CR0[3:0] --------------------------
    movw $5, %ax                  # PE | EM
    lmsw %ax
    smsw %bx
    movzwl %bx, %ebx
    andl $0xf, %ebx
    cmpl $5, %ebx
    jne bad5

    # --- STR returns a 16-bit selector ----------------------------------
    str %ax
    movzwl %ax, %eax
    cmpl $0xffff, %eax            # must be a real selector, not 0xffff/0xffffffff
    je bad6

    # Restore the loader descriptors before finishing.
    lgdt 0x100(%r14)
    lidt 0x120(%r14)

    movq $0x64, %r12
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

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish
