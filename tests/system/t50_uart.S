.text
.globl _start
_start:
    movq $0x800000, %rsp

    # Configure COM1: 115200 8N1, FIFO off
    movw $0x3f9, %dx
    movb $0x00, %al
    outb %al, %dx              # IER = 0
    movw $0x3fb, %dx
    movb $0x80, %al
    outb %al, %dx              # LCR.DLAB = 1
    movw $0x3f8, %dx
    movb $0x01, %al
    outb %al, %dx              # DLL = 1 (115200)
    movw $0x3f9, %dx
    movb $0x00, %al
    outb %al, %dx              # DLM = 0
    movw $0x3fb, %dx
    movb $0x03, %al
    outb %al, %dx              # LCR: 8N1, DLAB = 0
    movw $0x3fa, %dx
    movb $0x07, %al
    outb %al, %dx              # FCR: enable + clear FIFOs

    # Print a known string through THR.
    leaq msg(%rip), %rsi
1:
    movb (%rsi), %al
    testb %al, %al
    jz 2f
    movw $0x3f8, %dx
    outb %al, %dx
    incq %rsi
    jmp 1b
2:

    # Verify LSR.THRE is set (transmitter empty).
    movw $0x3fd, %dx
    inb %dx, %al
    testb $0x20, %al
    jz bad_lsr

    # Verify LSR.DR is clear with no input pending.
    testb $0x01, %al
    jnz bad_lsr

    # Round-trip the scratch register.
    movw $0x3ff, %dx
    movb $0x5a, %al
    outb %al, %dx
    inb %dx, %al
    cmpb $0x5a, %al
    jne bad_scr

    # Read back the divisor latch.
    movw $0x3fb, %dx
    movb $0x80, %al
    outb %al, %dx
    movw $0x3f8, %dx
    inb %dx, %al
    cmpb $0x01, %al
    jne bad_dll
    movw $0x3fb, %dx
    movb $0x03, %al
    outb %al, %dx

    movq $0x50, %r12
    jmp finish

bad_lsr: movq $0x51, %r12
    jmp finish
bad_scr: movq $0x52, %r12
    jmp finish
bad_dll: movq $0x53, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish

msg:
    .asciz "XUART-OK\n"
