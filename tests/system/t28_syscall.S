.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r12, %r12
    xorq %r13, %r13

    leaq handler(%rip), %rax
    movq %rax, %rdx
    shrq $32, %rdx
    movl $0xc0000082, %ecx
    wrmsr

    syscall

resume:
    movq $1, %r13
    cmpq $1, %r12
    jne fail
    xorq %r13, %r13
fail:
    movq $0xf0000000, %r15
    movq %r13, (%r15)

handler:
    movq %rcx, %r14
    movl $0xc0000082, %ecx
    rdmsr
    movl %eax, %r8d
    shlq $32, %rdx
    orq %rdx, %r8
    leaq handler(%rip), %r9
    cmpq %r9, %r8
    jne bad
    movq $1, %r12
    movq %r14, %rcx
    sysretq
bad:
    movq $0xf0000000, %r15
    movq $0x77, (%r15)
