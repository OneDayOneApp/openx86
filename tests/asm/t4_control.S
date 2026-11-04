.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14

    # all 16 condition codes: taken / not-taken pairs
    movq $5, %rax
    movq $3, %rbx
    cmpq %rbx, %rax          # 5 - 3
    jg 1f
    addq $1, %r14
1:
    jle 2f
    addq $1, %r13
2:
    cmpq $5, %rax
    je 3f
    addq $1, %r14
3:
    jne 4f
    addq $1, %r13
4:
    cmpq %rax, %rbx          # 3 - 5
    jl 5f
    addq $1, %r14
5:
    jae 6f
    addq $1, %r14
6:
    jb 7f
    addq $1, %r13
7:
    testq %rax, %rax
    jns 8f
    addq $1, %r14
8:
    js 9f
    addq $1, %r14
9:
    jz 10f
    addq $1, %r14
10:
    jnz 11f
    addq $1, %r13
11:

    # loop with a function call
    movq $4, %r12
    xorq %r15, %r15
12:
    call add3
    decq %r12
    jnz 12b
    jmp 13f
add3:
    addq $3, %r15
    ret
13:

    # indirect call through a register
    leaq tail(%rip), %rax
    call *%rax
    jmp 14f
tail:
    addq $7, %r15
    ret
14:

    # indirect jump through a register
    leaq 15f(%rip), %rax
    jmp *%rax
    addq $0x100, %r15        # skipped
15:

    # nested calls
    call outer
    jmp 16f
outer:
    call inner
    ret
inner:
    addq $11, %r15
    ret
16:

    movq %r15, -8(%rsp)
    movq $0xf0000000, %r15
    movq $0, (%r15)
