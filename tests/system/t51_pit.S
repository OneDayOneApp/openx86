# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r12, %r12                # interrupt counter

    # IDT at 0x540000, vector 0x20 -> timer_gate (kernel CS 0x08)
    leaq timer_gate(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x8, %rdx
    shlq $16, %rdx
    orq %rdx, %rcx
    movq $0x8e, %rdx
    shlq $40, %rdx
    orq %rdx, %rcx
    movq $0x540000, %rbx
    movq %rcx, 0x200(%rbx)         # vector 0x20 * 16
    movq %rax, %rdx
    shrq $32, %rdx
    movq %rdx, 0x208(%rbx)
    movw $0x0fff, 0x540000
    movq $0x540000, 0x540002
    movq $0x540000, %rax
    lidt (%rax)

    # --- 8259A init: master only, vector base 0x20 ---
    movw $0x20, %dx
    movb $0x11, %al
    outb %al, %dx                  # ICW1: edge, init, ICW4 needed
    movw $0x21, %dx
    movb $0x20, %al
    outb %al, %dx                  # ICW2: vector base 0x20
    movw $0x21, %dx
    movb $0x04, %al
    outb %al, %dx                  # ICW3: slave on IRQ2 (none here)
    movw $0x21, %dx
    movb $0x01, %al
    outb %al, %dx                  # ICW4: 8086 mode
    movw $0x21, %dx
    movb $0xfe, %al
    outb %al, %dx                  # OCW1: unmask IRQ0 only

    # --- 8254 PIT channel 0: mode 2, reload 2000 ---
    movw $0x43, %dx
    movb $0x34, %al
    outb %al, %dx                  # ch0, LSB then MSB, mode 2, binary
    movw $0x40, %dx
    movb $0xd0, %al
    outb %al, %dx                  # LSB = 0x00d0 (2000 low)
    movw $0x40, %dx
    movb $0x07, %al
    outb %al, %dx                  # MSB = 0x07 (2000 high)

    sti

    # Wait for at least 3 timer interrupts.
    movq $4000000, %rcx
wait_loop:
    decq %rcx
    jnz wait_loop

    cli
    cmpq $3, %r12
    jb bad_count

    # Verify the mask register reads back what we wrote.
    movw $0x21, %dx
    inb %dx, %al
    cmpb $0xfe, %al
    jne bad_mask

    movq $0x60, %r12
    jmp finish

bad_count:
    movq $0x61, %r12
    jmp finish
bad_mask:
    movq $0x62, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish

timer_gate:
    incq %r12
    # EOI to the master PIC
    movw $0x20, %dx
    movb $0x20, %al
    outb %al, %dx
    iretq
