.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $5, %rax
    movq $3, %rbx
    cmpq %rbx, %rax          # 5 - 3
    setg %r8b
    setl %r9b
    sete %r10b
    setne %r11b
    seta %r12b
    setb %r13b
    setae %r14b
    setbe %r15b
    movb %r8b, -1(%rsp)
    movb %r9b, -2(%rsp)
    movb %r10b, -3(%rsp)
    movb %r11b, -4(%rsp)
    movb %r12b, -5(%rsp)
    movb %r13b, -6(%rsp)
    movb %r14b, -7(%rsp)
    movb %r15b, -8(%rsp)

    cmpq %rax, %rbx          # 3 - 5
    setg %r8b
    setl %r9b
    sete %r10b
    setne %r11b
    seta %r12b
    setb %r13b
    setae %r14b
    setbe %r15b
    movb %r8b, -9(%rsp)
    movb %r9b, -10(%rsp)
    movb %r10b, -11(%rsp)
    movb %r11b, -12(%rsp)
    movb %r12b, -13(%rsp)
    movb %r13b, -14(%rsp)
    movb %r14b, -15(%rsp)
    movb %r15b, -16(%rsp)

    testq %rax, %rax
    sets %r8b
    setns %r9b
    setz %r10b
    setnz %r11b
    seto %r12b
    setno %r13b
    movb %r8b, -17(%rsp)
    movb %r9b, -18(%rsp)
    movb %r10b, -19(%rsp)
    movb %r11b, -20(%rsp)
    movb %r12b, -21(%rsp)
    movb %r13b, -22(%rsp)

    cmpq $-1, %rax           # 5 - (-1)
    setl %r8b
    setg %r9b
    movb %r8b, -23(%rsp)
    movb %r9b, -24(%rsp)

    movq $0xf0000000, %r15
    movq $0, (%r15)
