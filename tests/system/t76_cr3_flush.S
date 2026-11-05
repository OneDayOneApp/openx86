# t76_cr3_flush -- writing CR3 with an unchanged value must still flush the TLB.
#
# Linux flushes the TLB by reloading CR3 with its current value
# (flush_tlb_local() -> native_write_cr3(__native_read_cr3())).  After fork(),
# dup_mmap() ends with flush_tlb_mm(oldmm) after copy_page_range() has
# write-protected the parent's COW pages; that same-value CR3 load is what
# retires the stale writable translations of the parent.  If XEMU only
# flushes when the CR3 value changes, those entries stay writable; the parent
# then writes straight into pages it still shares with the child, and the
# child's copy (its own COW fault copy) contains the parent's later stack
# stores.  That is the observed busybox "segfault at 1": the child's fork
# wrapper popped return address 1 out of the shared user stack.
#
# The test reproduces the exact emulator state without needing fork():
#   1. at CPL0, write a user page (0x603000) so the TLB caches it writable
#   2. clear the leaf PTE R/W bit (as copy_page_range does for COW)
#   3. reload CR3 with the same value (the architectural full flush)
#   4. at CPL3 the write must now #PF with P=1|W=1|U=1 = 0x7; before the fix
#      the stale writable TLB entry silently allowed the store
#   5. the #PF handler restores R/W, invlpg's and iretq's, so the retried
#      store must succeed; int 0x80 then verifies the final data
#
# Exit 0x76 on success, 0x7N on a failed check.

.text
.globl _start
_start:
    movq $0x8ff000, %rsp

    # ---- page tables at 0x900000 ---------------------------------------
    movq $0x901007, 0x900000          # PML4[0] -> PDPT (P|RW|U)
    movq $0x902007, 0x901000          # PDPT[0] -> PD   (P|RW|U)
    movq $0x904003, 0x901018          # PDPT[3] -> halt PD
    movq $0x00000083, 0x902000        # PD[0] 0..2 MiB   (U=0)
    movq $0x00200083, 0x902008        # PD[1] 2..4 MiB
    movq $0x00400083, 0x902010        # PD[2] 4..6 MiB
    movq $0x903007, 0x902018          # PD[3] 6..8 MiB -> PT 0x903000 (U=1)
    movq $0x00800083, 0x902020        # PD[4] 8..10 MiB (tables, stack)
    movq $0x600007, 0x903000          # 0x600000 user code   U=1 RW
    movq $0x601007, 0x903008          # 0x601000 user stack  U=1 RW
    movq $0x603007, 0x903018          # 0x603000 user data   U=1 RW
    movabsq $0xF0000083, %rax         # halt MMIO 0xF0000000
    movq %rax, 0x904C00

    # ---- GDT + TSS at 0x905000 / 0x908000 ------------------------------
    movabsq $0x00af9a000000ffff, %r11
    movq %r11, 0x905008               # kernel code 0x08
    movabsq $0x00af92000000ffff, %r11
    movq %r11, 0x905010               # kernel data 0x10
    movabsq $0x00cffa000000ffff, %r11
    movq %r11, 0x905018               # user code 0x1b
    movabsq $0x00cff2000000ffff, %r11
    movq %r11, 0x905020               # user data 0x23
    movabsq $0x0000899080000067, %r11
    movq %r11, 0x905028               # TSS 0x28, base 0x908000, limit 0x67
    movw $0x2f, 0x905F00
    movq $0x905000, 0x905F02
    movq $0x905F00, %rax
    lgdt (%rax)
    movw $0x28, %ax
    ltr %ax
    movq $0x8ff000, 0x908004          # TSS RSP0

    # ---- IDT at 0x906000: v14 -> pf_handler, v0x80 -> user_gate ---------
    leaq pf_handler(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0x8e0000080000, %rdx        # type_attr=0x8e, sel=0x08
    orq %rdx, %rcx
    movq %rcx, 0x906000 + 14*16
    shrq $32, %rax
    movq %rax, 0x906000 + 14*16 + 8

    leaq user_gate(%rip), %rax
    movq %rax, %rcx
    andq $0xffff, %rcx
    movq %rax, %rdx
    shrq $16, %rdx
    andq $0xffff, %rdx
    shlq $48, %rdx
    orq %rdx, %rcx
    movq $0xef0000080000, %rdx        # type_attr=0xef, sel=0x08 (DPL3)
    orq %rdx, %rcx
    movq %rcx, 0x906000 + 0x80*16
    shrq $32, %rax
    movq %rax, 0x906000 + 0x80*16 + 8

    movw $0x0fff, 0x906F00
    movq $0x906000, 0x906F02
    movq $0x906F00, %rax
    lidt (%rax)

    # ---- relocate the CPL3 payload to 0x600000 -------------------------
    leaq user_entry(%rip), %rsi
    movq $0x600000, %rdi
    movl $user_end-user_entry, %ecx
    cld
    rep movsb

    # ---- turn on paging + long mode ------------------------------------
    movq %cr4, %rax
    orq $0x20, %rax                   # PAE
    movq %rax, %cr4
    movq $0x900000, %rax
    movq %rax, %cr3
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax                  # LME
    wrmsr
    movq %cr0, %rax
    movabsq $0x80000001, %rdx         # PG | PE
    orq %rdx, %rax
    movq %rax, %cr0

    # ---- establish a writable TLB entry for the user data page ---------
    movq $0, 0x908010                 # #PF counter
    movq $0x1111, 0x603000            # CPL0 store: caches 0x603000 writable

    # ---- emulate copy_page_range()'s write-protect for COW -------------
    movq $0x603005, 0x903018          # leaf PTE: P|U, R/W cleared

    # ---- the architectural full TLB flush: CR3 <- CR3 (same value) -----
    movq %cr3, %rax
    movq %rax, %cr3

    # ---- enter CPL3 ----------------------------------------------------
    pushq $0x23                       # user SS
    pushq $0x601ff0                   # user RSP
    pushq $0x202
    pushq $0x1b                       # user CS
    pushq $0x600000
    iretq

# ---- CPL3 payload (position independent, copied to 0x600000) -----------
user_entry:
    # r8/r9 survive the #PF handler (it must not clobber them: iret does not
    # restore general registers, only RIP/CS/RFLAGS/RSP/SS).
    movabsq $0x603000, %r8
    movq $0x7676, %r9
    movq %r9, (%r8)                   # must #PF: PTE is read-only
    int $0x80                         # back to CPL0
user_end:

# ---- #PF handler -------------------------------------------------------
pf_handler:
    # frame: 0(%rsp)=err, 8=rip, 16=cs, 24=rflags, 32=rsp, 40=ss
    cmpq $0x1b, 16(%rsp)
    jne fail_cs
    movq 0(%rsp), %rax
    cmpq $0x7, %rax                   # P=1, W=1, U=1 (protection write)
    jne fail_err
    movq %cr2, %rcx
    movabsq $0x603000, %rdx
    cmpq %rdx, %rcx
    jne fail_cr2

    movq 0x908010, %rdx
    incq %rdx
    movq %rdx, 0x908010
    cmpq $1, %rdx
    jne fail_count

    # make the page writable again and retire the stale translation
    movq $0x603007, 0x903018
    movq $0x603000, %rax
    invlpg (%rax)

    addq $8, %rsp                     # drop the error code
    iretq                             # retry the CPL3 store

fail_cs:
    movq $0xf0000000, %r15
    movq $0x79, %rax
    movq %rax, (%r15)
    jmp 9f
fail_err:
    movq $0xf0000000, %r15
    movq $0x7a, %rax
    movq %rax, (%r15)
    jmp 9f
fail_cr2:
    movq $0xf0000000, %r15
    movq $0x7b, %rax
    movq %rax, (%r15)
    jmp 9f
fail_count:
    movq $0xf0000000, %r15
    movq $0x7c, %rax
    movq %rax, (%r15)

# ---- int 0x80 gate: verifies the retried store actually landed ---------
user_gate:
    cmpq $0x1b, 8(%rsp)               # frame: 0=rip, 8=cs, 16=rflags...
    jne gate_fail
    cmpq $1, 0x908010                 # exactly one #PF must have happened
    jne gate_fail
    movabsq $0x603000, %rax
    movabsq $0x7676, %rcx
    cmpq %rcx, (%rax)
    jne gate_fail
    movq $0xf0000000, %r15
    movq $0x76, %rax
    movq %rax, (%r15)
2:  jmp 2b

gate_fail:
    movq $0xf0000000, %r15
    movq $0x7f, %rax
    movq %rax, (%r15)
3:  jmp 3b

9:  jmp 9b
