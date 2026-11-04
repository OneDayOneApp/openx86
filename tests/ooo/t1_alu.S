.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0x1122334455667788, %rax
    movq $0x1000, %rbx
    addq %rax, %rbx
    subq $0x200, %rbx
    andq $0xff00, %rbx
    orq $0x55, %rbx
    xorq %rax, %rbx
    shlq $3, %rbx
    shrq $2, %rbx
    sarq $1, %rbx
    leaq 16(%rsp,%rax,2), %rdx
    movq $3, %rcx
    shlq %cl, %rbx
    incq %rbx
    decq %rbx
    negq %rbx
    notq %rbx
    hlt
