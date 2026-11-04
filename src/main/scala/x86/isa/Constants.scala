package x86.isa

/** Decoded operation identifiers. Kept as plain integers so they can live in ROM tables. */
object Ops {
  val NOP = 0
  val MOV = 1
  val MOVZX = 2
  val MOVSX = 3
  val MOVSXD = 4
  val LEA = 5

  val ADD = 6
  val OR = 7
  val ADC = 8
  val SBB = 9
  val AND = 10
  val SUB = 11
  val XOR = 12
  val CMP = 13
  val TEST = 14

  val INC = 15
  val DEC = 16
  val NEG = 17
  val NOT = 18
  val MUL = 19
  val IMUL = 20
  val DIV = 21
  val IDIV = 22

  val SHL = 23
  val SHR = 24
  val SAR = 25
  val ROL = 26
  val ROR = 27
  val RCL = 28
  val RCR = 29

  val JMP = 30
  val JCC = 31
  val CALL = 32
  val RET = 33
  val PUSH = 34
  val POP = 35
  val LOOP = 36
  val SETCC = 37
  val CMOVCC = 38

  val CDQ = 39 // CWD/CDQ/CQO
  val CBW = 40 // CBW/CWDE/CDQE
  val HLT = 41
  val SYSCALL = 42
  val CPUID = 43
  val UD2 = 44
  val ENDBR = 45

  val MOVS = 46
  val STOS = 47
  val LODS = 48
  val SCAS = 49
  val CMPS = 50

  val PUSHF = 51
  val POPF = 52
  val LAHF = 53
  val SAHF = 54
  val CLC = 55
  val STC = 56
  val CMC = 57
  val CLD = 58
  val STD = 111
  val CLI = 60
  val STI = 61

  val XADD = 59
  val XCHG = 62
  val INT = 63
  val INT3 = 64

  // ---- SSE / SSE2 ----
  val MOVD = 70 // GPR <-> XMM, 32/64 bit
  val MOVQ = 71 // XMM <-> m64, and XMM <-> XMM
  val MOVDQ = 72 // MOVDQA/MOVDQU 128-bit
  val MOVPS = 73 // MOVAPS/MOVUPS/MOVSS/MOVSD
  val SIMD_ADD = 74
  val SIMD_SUB = 75
  val SIMD_MUL = 76
  val SIMD_DIV = 77
  val SIMD_SQRT = 78
  val SIMD_MIN = 79
  val SIMD_MAX = 80
  val SIMD_AND = 81
  val SIMD_ANDN = 82
  val SIMD_OR = 83
  val SIMD_XOR = 84
  val SIMD_UCOMI = 85
  val SIMD_COMI = 86
  val SIMD_PADDD = 87
  val SIMD_PSUBD = 88
  val SIMD_PAND = 89
  val SIMD_POR = 90
  val SIMD_PXOR = 91

  val CVTSI2SD = 92
  val CVTSI2SS = 93
  val CVTTSD2SI = 94
  val CVTTSS2SI = 95
  val CVTSS2SD = 96
  val CVTSD2SS = 97
  val CVTDQ2PS = 98
  val CVTPS2DQ = 99
  val CVTDQ2PD = 100
  val CVTPD2DQ = 101
  val CVTSD2SI = 102
  val CVTTPD2DQ = 103

  val MOV_CR = 104
  val RDMSR = 105
  val WRMSR = 106
  val SYSRET = 107
  val IRET = 108
  val LIDT = 109
  val INVLPG = 110
  val LOOPNE = 112
  val LOOPE = 113
  val JRCXZ = 114
  val UNDEF = 255
}

/** Operand kinds stored in a [[x86.isa.Operand]]. */
object OperandKind {
  val None = 0
  val Gpr = 1
  val Xmm = 2
  val Mem = 3
  val Imm = 4
}

/** SIMD data type selector. */
object SimdKind {
  val None = 0
  val SS = 1 // scalar single
  val SD = 2 // scalar double
  val PS = 3 // packed single
  val PD = 4 // packed double
  val DQ = 5 // packed integer / 128-bit
}

/** Immediate size classes produced by the decode ROM. */
object ImmKind {
  val None = 0
  val Imm8 = 1
  val ImmZ = 2 // 16/32 depending on operand size (sign extended to 64 in long mode)
  val Imm32 = 3
  val Imm64 = 4 // 64-bit only with REX.W, otherwise 32
  val Imm16 = 5
}

/** Special decode forms not expressible with the regular operand rules. */
object DecodeForm {
  val Normal = 0
  val Movzx8 = 1
  val Movzx16 = 2
  val Movsx8 = 3
  val Movsx16 = 4
  val Movsxd = 5
  val Acc = 6 // accumulator operand (AL/AX/EAX/RAX) with immediate
  val Special = 7
}

object OpcodeGroup {
  val None = 0
  val Grp1 = 1 // 80/81/82/83: ALU with immediate
  val Grp2 = 2 // C0/C1/D0-D3: shifts
  val Grp3 = 3 // F6/F7
  val Grp4 = 4 // FE
  val Grp5 = 5 // FF
  val Grp11 = 6 // C6/C7: mov with immediate
  val Grp1A = 7 // 8F: pop
}

object Cond {
  val O = 0
  val NO = 1
  val B = 2
  val AE = 3
  val E = 4
  val NE = 5
  val BE = 6
  val A = 7
  val S = 8
  val NS = 9
  val P = 10
  val NP = 11
  val L = 12
  val GE = 13
  val LE = 14
  val G = 15
}

/** RFLAGS bit positions (subset that this core models). */
object FlagBit {
  val CF = 0
  val PF = 2
  val AF = 4
  val ZF = 6
  val SF = 7
  val TF = 8
  val IF = 9
  val DF = 10
  val OF = 11
}

object ExceptionCause {
  val None = 0
  val De = 1 // #DE divide error
  val Ud = 2 // #UD invalid opcode
  val Gp = 3 // #GP general protection
  val Pf = 4 // #PF page fault
  val Bp = 5 // #BP breakpoint
  val Ss = 6 // #SS stack fault
}
