# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
# BT/BTS/BTR/BTC with a memory destination and bit offsets past the operand
# size.  The effective address must advance by (offset DIV operand_size)
# elements while the index is offset MOD operand_size -- Bochs does exactly
# this (cpu/bit64.cc: index = op & 0x3f; addr += 8 * ((op & ~0x3f) / 64)).
#
# Unicorn gets this wrong (it keeps the whole offset modulo the operand size
# at the base address), so this case is covered here rather than in isa-diff.
# XEMU's earlier "mask only" behaviour silently corrupted kernel bitmaps: it
# marked IDT vectors 0x30..0x37 as "system vectors", Linux therefore never
# installed the PIC IRQ0 gate, and the first timer interrupt jumped to 0.
#
# Result goes to the halt MMIO; 0x63 means every check passed.

.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x600000, %r14          # scratch buffer (loader zeroes RAM)

    # --- BTS qword, offset 100 -> element 1, bit 36 ----------------------
    btsq $100, 0x10(%r14)
    movq 0x10(%r14), %rax
    cmpq $0, %rax
    jne bad1                      # element 0 must stay untouched
    movq 0x18(%r14), %rax
    movq $0x1000000000, %rcx      # 1 << 36
    cmpq %rcx, %rax
    jne bad2
    jc bad3                       # previous bit value was 0

    btsq $100, 0x10(%r14)         # same bit again: CF must now be 1
    jnc bad4

    # --- BT qword, offset 200 in a register -> element 3, bit 8 ---------
    movq $200, %rcx
    btsq %rcx, 0x10(%r14)
    btq %rcx, 0x10(%r14)
    jnc bad5
    movq 0x28(%r14), %rax         # 0x10 + 3*8 = 0x28
    cmpq $0x100, %rax
    jne bad6

    # --- BTR/BTC cross elements the same way ----------------------------
    btrq %rcx, 0x10(%r14)
    jnc bad7                      # bit was set, so clearing reports CF=1
    movq 0x28(%r14), %rax
    cmpq $0, %rax
    jne bad8

    movq $300, %rcx
    btcq %rcx, 0x10(%r14)         # element 4, bit 44
    movq 0x30(%r14), %rax
    movq $0x100000000000, %rcx
    cmpq %rcx, %rax
    jne bad9

    # --- Negative (sign-extended imm8) offset goes to the previous ------
    #     element, matching Bochs: (-8 & ~63) / 64 = -1, index 56.
    btcq $-8, 0x10(%r14)
    movq 0x08(%r14), %rax
    movq $0x0100000000000000, %rcx
    cmpq %rcx, %rax
    jne bad16

    # --- 32-bit operand: element stride 4 bytes, index mod 32 -----------
    movq $0, 0x10(%r14)
    movq $0, 0x18(%r14)
    btsl $70, 0x10(%r14)          # element 2 (offset 8), bit 6
    movl 0x18(%r14), %eax
    cmpl $0x40, %eax
    jne bad10
    movl 0x10(%r14), %eax
    cmpl $0, %eax
    jne bad11

    # --- 16-bit operand: element stride 2 bytes, index mod 16 -----------
    movq $0, 0x40(%r14)
    btsw $40, 0x40(%r14)          # element 2 (offset 4), bit 8
    movzwl 0x44(%r14), %eax
    cmpl $0x100, %eax
    jne bad12
    movzwl 0x40(%r14), %eax
    cmpl $0, %eax
    jne bad13

    # --- Register destination still masks the offset --------------------
    movq $0, %rax
    movq $70, %rcx
    btsq %rcx, %rax               # index 6 of the register itself
    movq $0x40, %rdx
    cmpq %rdx, %rax
    jne bad14

    # --- Immediate form with a memory operand ---------------------------
    movq $0, 0x50(%r14)
    btsq $127, 0x50(%r14)         # element 1, bit 63
    movq 0x58(%r14), %rax
    movq $0x8000000000000000, %rcx
    cmpq %rcx, %rax
    jne bad15

    movq $0x63, %r12
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
    jmp finish
bad8:  movq $0x08, %r12
    jmp finish
bad9:  movq $0x09, %r12
    jmp finish
bad10: movq $0x0a, %r12
    jmp finish
bad11: movq $0x0b, %r12
    jmp finish
bad12: movq $0x0c, %r12
    jmp finish
bad13: movq $0x0d, %r12
    jmp finish
bad14: movq $0x0e, %r12
    jmp finish
bad15: movq $0x0f, %r12
    jmp finish
bad16: movq $0x10, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish
