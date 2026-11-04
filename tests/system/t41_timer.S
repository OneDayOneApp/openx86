.text
.globl _start
_start:
    movq $0x800000, %rsp
    movq $0, %r12                  # IRQ counter

    # IDT at 0x540000: vector 32 (timer) -> timer_gate
    leaq timer_gate(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x8, %rdx                # kernel CS
    shlq $16, %rdx
    orq %rdx, %rcx
    movq $0x8e, %rdx               # present, DPL0, interrupt gate
    shlq $40, %rdx
    orq %rdx, %rcx
    movq $0x540000, %rbx
    movq $0x540000, %rdi
    addq $0x200, %rdi              # vector 32 * 16
    movq %rcx, (%rdi)
    movq %rax, %rdx
    shrq $32, %rdx
    movq %rdx, 8(%rdi)
    movw $0x0fff, 0x540000
    movq $0x540000, 0x540002
    movq $0x540000, %rax
    lidt (%rax)

    # program timer: period 20, enable
    movq $0xe0000000, %r15
    movq $20, %rax
    movq %rax, 8(%r15)             # period
    movq $1, %rax
    movq %rax, 12(%r15)            # enable

    # busy loop until 3 interrupts fired
    movq $200, %rcx
wait_loop:
    decq %rcx
    jnz wait_loop

    cmpq $3, %r12
    jb bad
    movq $0x207, %rax
    jmp finish
bad:
    movq $0xbad, %rax
finish:
    movq $0xf0000000, %r15
    movq %rax, (%r15)
    jmp finish

timer_gate:
    incq %r12
    movq $0xe0000000, %r15
    movq $0, %rax
    movq %rax, 20(%r15)            # clear pending
    iretq
