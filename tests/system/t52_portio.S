.text
.globl _start
_start:
    movq $0x800000, %rsp

    # --- IN/OUT width semantics ------------------------------------------
    # Write a byte to the UART scratch register, read it back as a byte.
    movw $0x3ff, %dx
    movb $0xa5, %al
    outb %al, %dx
    inb %dx, %al
    cmpb $0xa5, %al
    jne bad1

    # Preserve the upper bits when a byte IN targets a full register.
    movq $0x1122334455667788, %rax
    movw $0x3ff, %dx
    inb %dx, %al
    movq $0x11223344556677a5, %rcx
    cmpq %rcx, %rax
    jne bad2

    # 16-bit IN writes AX only and preserves the upper 48 bits.
    movq $0xffffffffffffffff, %rax
    movw $0x3ff, %dx
    inw %dx, %ax
    movq $0xffffffffffff00a5, %rcx
    cmpq %rcx, %rax
    jne bad3

    # 32-bit IN writes the full register (zero-extended).
    movq $0xffffffffffffffff, %rax
    movw $0x3ff, %dx
    inl %dx, %eax
    movq $0x00000000000000a5, %rcx
    cmpq %rcx, %rax
    jne bad4

    # --- Unmapped port reads return all ones -----------------------------
    movw $0x9999, %dx
    inb %dx, %al
    cmpb $0xff, %al
    jne bad5
    inl %dx, %eax
    cmpl $0xffffffff, %eax
    jne bad6

    # Writing an unmapped port is a no-op.
    movb $0x11, %al
    outb %al, %dx
    inb %dx, %al
    cmpb $0xff, %al
    jne bad7

    # --- LSR semantics after transmit ------------------------------------
    movw $0x3fd, %dx
    inb %dx, %al
    testb $0x20, %al               # THRE must be set
    jz bad8
    testb $0x01, %al               # DR must be clear (no input)
    jnz bad8

    movq $0x70, %r12
    jmp finish

bad1: movq $0x71, %r12
    jmp finish
bad2: movq $0x72, %r12
    jmp finish
bad3: movq $0x73, %r12
    jmp finish
bad4: movq $0x74, %r12
    jmp finish
bad5: movq $0x75, %r12
    jmp finish
bad6: movq $0x76, %r12
    jmp finish
bad7: movq $0x77, %r12
    jmp finish
bad8: movq $0x78, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish
