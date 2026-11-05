.text
.globl _start
_start:
    movq $0x800000, %rsp
    xorq %r12, %r12

    movl $0, %eax
    cpuid
    cmpl $1, %eax
    jne fail
    cmpl $0x6e65704f, %ebx             # "Open"
    jne fail
    cmpl $0x65726f43, %ecx             # "Core"
    jne fail
    cmpl $0x2d363858, %edx             # "X86-"
    jne fail

    movl $1, %eax
    cpuid
    andl $0x04000000, %ecx
    cmpl $0x04000000, %ecx
    jne fail
    andl $0x04000000, %edx
    cmpl $0x04000000, %edx
    jne fail

    movl $0x80000008, %eax
    cpuid
    cmpl $0x00003028, %eax
    jne fail

    movl $0x80000001, %eax
    cpuid
    andl $0x20000000, %edx
    cmpl $0x20000000, %edx
    jne fail
    jmp ok
fail:
    movq $1, %r12
ok:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
