.text
.globl _start
_start:
    movq $0x800000, %rsp

    movq $0x500000, %rbx
    movq $0x501003, 0x0000(%rbx)
    movq $0x502003, 0x1000(%rbx)
    movq $0x503003, 0x2010(%rbx)
    movq $0x504003, 0x2018(%rbx)
    movq $0x400003, 0x3000(%rbx)          # VA 0x400000 -> PA 0x400000 (code)
    movq $0x700003, 0x4000(%rbx)
    movq $0x504003, 0x503900              # PT[0x120]: VA 0x520000 -> PA 0x504000
    movq $0x7ff003, 0x4ff8(%rbx)

    movl $0x12345678, 0x700000
    movl $0x55, 0x710000

    movq $0x500000, %rax
    movq %rax, %cr3
    movq $0x80000001, %rax
    movq %rax, %cr0

    movl 0x600000, %r8d                   # cached translation -> 0x12345678
    movl $0x710003, 0x520000              # remap VA 0x600000 -> PA 0x710000
    invlpg 0x600000
    movl 0x600000, %r9d                   # should be 0x55

    movq $1, %rax
    movq %rax, %cr0

    movq $1, %r12
    cmpl $0x12345678, %r8d
    jne fail
    cmpl $0x55, %r9d
    jne fail
    xorq %r12, %r12
fail:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
