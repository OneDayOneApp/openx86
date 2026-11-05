# t61_early_idt -- regression test for the early-IDT handler entry contract.
#
# Bottom-up level: exception delivery + the vector-on-stack stub layout that
# Linux's `early_idt_handler_array` uses.
#
# Linux builds each vector's stub as
#     pushq $vector            (plus a dummy error code when the CPU pushes none)
#     jmp   early_idt_handler_common
# and the common handler reads the vector with `mov 8(%rsp), %rsi`.
#
# If XEMU advances RIP by the instruction length after delivering the
# exception, the handler is entered at `pushq $vector`+len instead, the push
# is skipped, and the vector read off the stack is garbage. That is exactly
# the defect that made the kernel take the wrong branch into its panic path.
#
# Exit 0x61 on success, 0x6N on the N-th failed check.

.text
.globl _start
_start:
    movq $0x800000, %rsp

    # page tables: identity 0..10 MiB, nothing at 10 MiB
    movq $0x501003, 0x500000
    movq $0x502003, 0x501000
    movq $0x00000083, 0x502000
    movq $0x00200083, 0x502008
    movq $0x00400083, 0x502010
    movq $0x00600083, 0x502018
    movq $0x00800083, 0x502020
    movq $0, 0x502028                 # 10 MiB absent

    # halt MMIO at 0xF0000000 lives under PDPT[3]
    movq $0x503003, 0x501018
    movq $0, 0x503000
    movabsq $0xF0000083, %rax
    movq %rax, 0x503C00

    # IDT: vector 14 -> stub14, which pushes the vector then jumps to common
    leaq stub14(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x8e0000080000, %rdx
    orq %rdx, %rcx
    movq %rcx, 0x5A0000 + 14*16
    movq $0, 0x5A0000 + 14*16 + 8
    movw $0xFF, 0x5A8000
    movq $0x5A0000, 0x5A8002
    movq $0x5A8000, %rax
    lidt (%rax)

    # enable paging
    movq %cr4, %rax
    orq $0x20, %rax
    movq %rax, %cr4
    movq $0x500000, %rax
    movq %rax, %cr3
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr
    movq %cr0, %rax
    movabsq $0x80000001, %rdx
    orq %rdx, %rax
    movq %rax, %cr0

    movq $0, %r12                     # marker set by the handler on success

    # touch the unmapped page: the handler maps it and the access is retried
    movq $0xA00000, %rax
probe_insn:
    movq $0x4242, (%rax)
    movq (%rax), %rbx
    cmpq $0x4242, %rbx
    jne bad2                          # the retry did not complete the store

    cmpq $1, %r12                     # handler saw vector 14 and CR2 == probe
    jne bad1

    movq $0x61, %r12
    jmp finish

bad1: movq $0x62, %r12
    jmp finish
bad2: movq $0x63, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish

# Linux-style stub: #PF pushes an error code, so only the vector is pushed.
stub14:
    pushq $14
    jmp common

common:
    cmpq $14, (%rsp)                  # vector must be on top of the stack
    jne bad_handler
    addq $8, %rsp                     # drop the vector
    movq 8(%rsp), %r13                # saved RIP must be the faulting insn
    leaq probe_insn(%rip), %rdx       # position independent: works at any load base
    cmpq %rdx, %r13
    jne bad_handler

    # CR2 must be the probe address
    movq %cr2, %rax
    cmpq $0xA00000, %rax
    jne bad_handler

    # map the missing 2 MiB page
    movq %rax, %rdx
    shrq $21, %rdx
    movq %rdx, %r8
    shlq $21, %r8
    orq $0x83, %r8
    movq $0x502000, %rcx
    movq %r8, (%rcx,%rdx,8)
    invlpg (%rax)

    movq $1, %r12                     # all handler-side checks passed
    addq $8, %rsp                     # drop the hardware error code
    iretq

bad_handler:
    movq $0, %r12
    jmp finish
