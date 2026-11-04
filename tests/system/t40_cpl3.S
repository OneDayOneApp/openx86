.text
.globl _start
_start:
    movq $0x800000, %rsp

    # GDT at 0x510000: null, kernel CS (0x08), kernel SS (0x10), user CS (0x18|3), user SS (0x20|3), TSS (0x28)
    movq $0x510000, %rbx
    movq $0, 0x00(%rbx)
    movabsq $0x00af9a000000ffff, %r11
    movq %r11, 0x08(%rbx)                     # kernel code 64-bit
    movabsq $0x00af92000000ffff, %r11
    movq %r11, 0x10(%rbx)                     # kernel data
    movabsq $0x00cffa000000ffff, %r11
    movq %r11, 0x18(%rbx)                     # user code (DPL3)
    movabsq $0x00cff2000000ffff, %r11
    movq %r11, 0x20(%rbx)                     # user data (DPL3)
    # TSS descriptor at 0x28: limit=0x67, base=0x520000, type=0x9 (available)
    movabsq $0x0000895200000067, %r11
    movq %r11, 0x28(%rbx)

    # TSS at 0x520000: RSP0 at offset 4
    movq $0x7ff000, 0x520004

    movw $0x2f, 0x530000
    movq $0x510000, 0x530002
    movq $0x530000, %rax
    lgdt (%rax)
    movw $0x28, %ax
    ltr %ax

    # IDT at 0x540000, vector 0x80 -> user_gate
    leaq user_gate(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x18, %rdx          # user code selector
    shlq $16, %rdx
    orq %rdx, %rcx
    movq $0xef, %rdx          # present, DPL3, interrupt gate
    shlq $40, %rdx
    orq %rdx, %rcx
    movq $0x540000, %rbx
    movq $0x540800, %rdi
    movq %rcx, (%rdi)
    movq %rax, %rdx
    shrq $32, %rdx
    movq %rdx, 8(%rdi)
    movw $0x0fff, 0x540000
    movq $0x540000, 0x540002
    movq $0x540000, %rax
    lidt (%rax)

    # Switch to user mode: build an IRET frame
    movq $0x600000, %rax
    pushq $0x23               # user SS
    pushq $0x600000           # user RSP
    pushq $0x202              # RFLAGS
    pushq $0x1b               # user CS
    leaq user_entry(%rip), %rax
    pushq %rax
    iretq
user_entry:
    movq $0x1234, %r8
    int $0x80
    movq $0x5678, %r9
    jmp done
user_gate:
    movq $0x9abc, %r10
    iretq
done:
    movq $0xf0000000, %r15
    movq $0x7a, %rax
    movq %rax, (%r15)
    jmp done
