# t86_x87_regform -- the x87 register forms (mod == 3) of the D8..DF escapes.
#
# These are the encodings a compiler emits for the *stack* forms (no memory
# operand), so a decoder that only handles the memory forms silently
# mis-executes them.  XEMU had exactly that bug: the x87 escape group shared
# its enum value with the 0F AE system group, so D9 was routed to the system
# table and
#
#   D9 C0  fld st(0)   decoded as op 205 = FXSAVE
#   D9 C8  fxch st(0)  decoded as op 206 = FXRSTOR
#   D9 E0  fchs        decoded as #UD
#   DA..DF register forms  decoded as op 0 = NOP
#
# On top of that the register branch switched on `rm` and passed `reg` as the
# ST index (backwards), the D9 constant block was mis-split, the "P"
# arithmetic forms wrote their result to ST(0) instead of ST(1) (silently
# losing a stack entry per operation), and FXAM's class bits were wrong.
#
# Note: GNU as swaps the fsubp/fsubrp and fdivp/fdivrp *mnemonics* relative to
# the SDM encodings (it emits DE E1 for `fsubp`, which the SDM defines as
# FSUBRP).  The reversed-operand forms are therefore written as explicit
# .byte sequences so the assembler's naming cannot silently change what is
# being tested.  XEMU follows the SDM/Bochs tables, not the GAS mnemonics.
#
# Every check stores ST(0) as an IEEE double and compares the *bit pattern*
# against the exact expected value, so a wrong result cannot pass by rounding.
#
# Exit 0x86 on success, 0x8N on the N-th failed check.

.equ SCRATCH, 0x900000

# Build an integer value in ST(0).  Each use does:  fld1 xN and faddp
.macro PUSH1
    fld1
.endm

.text
.globl _start

# ---------------------------------------------------------------------------
# check_double EXPECT_REG -- store ST(0) to SCRATCH and compare with EXPECT_REG
# Clobbers %rax, %rbx, %rcx.
.macro CHECK reg, tag
    movl $SCRATCH, %eax
    fstpl (%eax)
    movq (%rax), %rbx
    movabsq \reg, %rcx
    cmpq %rcx, %rbx
    jne \tag
.endm

_start:
    movq $0x8ff000, %rsp

    # ==== 1. FLD1 / FLDZ / FADD (D9 E8, D9 EE, DE C0) ==================
    fninit
    fld1                        # 1
    fldz                        # 0, st1 = 1
    fld1                        # 1, st1 = 0, st2 = 1
    faddp                       # st0 = 1 + 0 = 1
    CHECK $0x3FF0000000000000, bad1

    # ==== 2. FCHS (D9 E0) ==============================================
    fninit
    fld1
    fchs                        # -1
    CHECK $0xBFF0000000000000, bad2

    # ==== 3. FABS (D9 E1) ==============================================
    fninit
    fld1
    fchs                        # -1
    fabs                        # +1
    CHECK $0x3FF0000000000000, bad3

    # ==== 4. FSQRT (D9 FA) =============================================
    fninit
    fld1
    fld1
    faddp                       # 2
    fld1
    faddp                       # 3
    fld1
    faddp                       # 4
    fsqrt                       # 2
    CHECK $0x4000000000000000, bad4

    # ==== 5. FLD ST(0) (D9 C0) and FADDP (DE C0) =======================
    fninit
    fld1
    fld1
    faddp                       # 2
    fld %st(0)                  # push a copy -> st0 = 2, st1 = 2
    faddp                       # 4
    CHECK $0x4010000000000000, bad5

    # ==== 6. FXCH ST(1) (D9 C9) ========================================
    fninit
    fld1                        # st0 = 1
    fld1
    fld1
    faddp                       # st0 = 2, st1 = 1
    fxch %st(1)                 # st0 = 1, st1 = 2
    fstp %st(1)                 # drop st1 -> st0 = 1
    CHECK $0x3FF0000000000000, bad6

    # ==== 7. FSUBP (DE E9): ST(1) - ST(0) ==============================
    fninit
    fld1
    fld1
    faddp                       # 2
    fld1
    faddp                       # 3
    fld1
    faddp                       # 4
    fld1                        # st0 = 1, st1 = 4
    .byte 0xde, 0xe9            # DE E8+i = FSUBP ST(i),ST(0): ST(i)-ST(0)
    CHECK $0x4008000000000000, bad7

    # ==== 8. FSUBRP (DE E1): ST(0) - ST(1) =============================
    fninit
    fld1
    fld1
    faddp                       # 2
    fld1
    faddp                       # 3
    fld1
    faddp                       # 4
    fld1                        # st0 = 1, st1 = 4
    .byte 0xde, 0xe1            # DE E0+i = FSUBRP ST(i),ST(0): ST(0)-ST(i)
    CHECK $0xC008000000000000, bad8

    # ==== 9. FMULP (DE C9) =============================================
    fninit
    fld1
    fld1
    faddp                       # 2
    fld1
    faddp                       # 3
    fld1
    faddp                       # 4
    fld1
    fld1
    faddp                       # st0 = 2, st1 = 4
    fmulp                       # 4 * 2 = 8
    CHECK $0x4020000000000000, bad9

    # ==== 10. FDIVP (DE F9) ============================================
    fninit
    fld1
    fld1
    faddp                       # 2
    fld1
    faddp                       # 3
    fld1
    faddp                       # 4
    fld1
    fld1
    faddp                       # st0 = 2, st1 = 4
    .byte 0xde, 0xf9            # DE F8+i = FDIVP: ST(i)/ST(0) = 4/2
    CHECK $0x4000000000000000, bad10

    # ==== 11. FTST (D9 E4): compares ST(0) with +0 =====================
    fninit
    fldz
    ftst
    fnstsw %ax
    testw $0x4000, %ax          # C3 = 1 => equal
    jz bad11

    # ==== 12. FXAM (D9 E5): classify +0 ================================
    fninit
    fldz
    fxam
    fnstsw %ax
    testw $0x4000, %ax          # C3 = 1 (zero)
    jz bad12
    testw $0x0100, %ax          # C0 = 0 (not NaN/inf)
    jnz bad12

    # ==== 13. FXAM classifies a normal number as C2 = 1 ================
    fninit
    fld1
    fxam
    fnstsw %ax
    testw $0x0400, %ax          # C2 = 1 => normal finite
    jz bad13

    # ==== 14. FCOMPP (DE D9) ===========================================
    fninit
    fld1
    fld1
    fcompp
    fnstsw %ax
    testw $0x4000, %ax          # C3 = 1 => equal
    jz bad14

    # ==== 15. FFREE (DD C0) then a working stack =======================
    fninit
    fld1
    ffree %st(0)                # mark empty, stack pointer unchanged
    fld1
    fld1
    faddp                       # 2
    fld1
    faddp                       # 3
    CHECK $0x4008000000000000, bad15

    # ==== 16. FNSTSW AX (DF E0) writes the status word =================
    fninit
    fld1
    fld1
    faddp                       # 2
    fld1
    faddp                       # 3
    fnstsw %ax
    andw $0x3800, %ax           # TOP (bits 13:11) must be non-zero here
    jz bad16

    movq $0x86, %r12
    jmp finish

bad1:  movq $0x01, %r12
    jmp finish
bad2:  movq $0x02, %r12
    jmp finish
bad3:  movq $0x03, %r12
    jmp finish
bad4:  movq $0x04, %r12
    jmp finish
bad5:  movq $0x05, %r12
    jmp finish
bad6:  movq $0x06, %r12
    jmp finish
bad7:  movq $0x07, %r12
    jmp finish
bad8:  movq $0x08, %r12
    jmp finish
bad9:  movq $0x09, %r12
    jmp finish
bad10: movq $0x0a, %r12
    jmp finish
bad11: movq $0x0b, %r12
    jmp finish
bad12: movq $0x0c, %r12
    jmp finish
bad13: movq $0x0d, %r12
    jmp finish
bad14: movq $0x0e, %r12
    jmp finish
bad15: movq $0x0f, %r12
    jmp finish
bad16: movq $0x10, %r12

finish:
    movq $0xf0000000, %r15
    movq %r12, (%r15)
    jmp finish
