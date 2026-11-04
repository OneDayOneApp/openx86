.text
.globl _start
_start:
    movabsq $0x123456789abcdef0, %rax
    hlt
