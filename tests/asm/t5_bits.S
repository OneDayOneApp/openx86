.text
.globl _start
_start:
    movq $0x800000, %rsp

    # 8/16/32-bit partial writes
    movq $0x1122334455667788, %rax
    movb $0xaa, %al
    movw $0xbbcc, %ax
    movl $0xdeadbeef, %eax
    movb %al, %bl
    movw %ax, %bx
    movl %eax, %ebx

    # byte/word arithmetic must preserve upper bits
    movq $0x1122334455667788, %rax
    addb $0x11, %al
    addw $0x2222, %ax
    subl $0x1111, %eax
    incb %al
    decw %ax

    # shifts, including CL and 8/16-bit widths
    movq $0x8000000000000001, %rax
    shlq $4, %rax
    shrq $1, %rax
    sarq $2, %rax
    movq $3, %rcx
    shlq %cl, %rax
    shrq %cl, %rax
    movb $0x81, %bl
    sarb $1, %bl
    shrb $1, %bl
    shlb $1, %bl
    rolq $7, %rax
    rorq $3, %rax
    rolw $3, %ax
    rorw $5, %ax

    # neg / not / cqto / cltq
    negq %rax
    notq %rax
    movq $-12345, %rax
    cqto
    cltq

    # multiply forms
    movq $123456789, %rax
    movq $987, %rbx
    imulq %rbx, %rax
    imulq $13, %rbx, %rcx
    imulq $-7, %rbx, %rdx

    # division (sign-extended dividend)
    movq $100000, %rax
    cqto
    movq $37, %rbx
    idivq %rbx
    xorq %rdx, %rdx
    movq $100000, %rax
    divq %rbx

    # setcc / cmov for all condition families
    cmpq %rbx, %rax
    sete %cl
    setne %dl
    setl %r8b
    setge %r9b
    seta %r10b
    setb %r11b
    movzbq %cl, %rcx
    movzbq %dl, %rdx
    movzbq %r8b, %r8
    movzbq %r9b, %r9
    movzbq %r10b, %r10
    movzbq %r11b, %r11
    cmovl %rax, %rbx
    cmovg %rax, %rcx
    cmove %rax, %rdx

    # movzx / movsx register forms
    movb $0xf0, %al
    movzbl %al, %ecx
    movsbl %al, %edx
    movzwq %ax, %r8
    movswq %ax, %r9
    movslq %eax, %r10

    movq %rcx, -8(%rsp)
    movq %rdx, -16(%rsp)
    movq %r8, -24(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)
