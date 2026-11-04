package x86.isa

/**
 * A single entry of the x86 decode ROM.
 *
 * The ROM encodes everything that can be decided from the opcode byte alone:
 * which operation to perform, whether a ModRM byte follows, the immediate
 * width class, operand ordering and group membership. Operand size prefixes and
 * the ModRM/SIB/displacement logic are handled by [[x86.frontend.Decoder]].
 */
case class DecodeInfo(
    op: Int,
    hasModRM: Boolean = true,
    imm: Int = ImmKind.None,
    dir: Int = 0, // 0: a = r/m, b = reg; 1: a = reg, b = r/m
    group: Int = OpcodeGroup.None,
    force8: Boolean = false,
    form: Int = DecodeForm.Normal,
    simd: Int = SimdKind.None,
    sseSel: Boolean = false, // derive SIMD kind from the mandatory SSE prefix
    store: Boolean = false   // SSE store direction (a = memory, b = XMM)
) {
  require(op >= 0 && op <= 255, s"op out of range: $op")
}

object DecodeInfo {
  val width = 24

  def pack(d: DecodeInfo): BigInt = {
    var v = BigInt(d.op & 0xff)
    def set(bit: Int, x: Boolean): Unit = if (x) v |= (BigInt(1) << bit)
    def setf(lo: Int, x: Int, w: Int): Unit = {
      val mask = ((BigInt(1) << w) - 1) << lo
      v = (v & ~mask) | ((BigInt(x) & (mask >> lo)) << lo)
    }
    set(8, d.hasModRM)
    setf(9, d.imm, 3)
    set(12, d.dir == 1)
    setf(13, d.group, 3)
    set(16, d.force8)
    setf(17, d.form, 3)
    setf(20, d.simd, 3)
    set(23, d.sseSel)
    set(24, d.store)
    v
  }

  /** Number of bits actually used (op..store). */
  val packedWidth = 25
}

object DecodeTables {
  import Ops._
  import ImmKind._
  import DecodeForm._

  private def undef = DecodeInfo(UNDEF, hasModRM = false)
  private def nop   = DecodeInfo(NOP, hasModRM = false)

  /** ALU op index (bits 5:3 of 0x00-0x3f) to operation. */
  private val aluOps = Array(ADD, OR, ADC, SBB, AND, SUB, XOR, CMP)

  private def buildPrimary(): Array[DecodeInfo] = {
    val t = Array.fill(256)(undef)

    // 0x00-0x3f: ALU r/m,r ; r,r/m ; AL/eAX,imm
    for (b <- 0x00 to 0x3f) {
      val low = b & 7
      if (low < 4) {
        val op = aluOps((b >> 3) & 7)
        val byte = (b & 1) == 0
        val dir = (b >> 1) & 1
        t(b) = DecodeInfo(op, hasModRM = true, dir = dir, force8 = byte)
      } else if (low == 4 || low == 5) {
        val op = aluOps((b >> 3) & 7)
        val byte = (b & 1) == 0
        t(b) = DecodeInfo(
          op,
          hasModRM = false,
          imm = if (byte) Imm8 else ImmZ,
          dir = 1,
          force8 = byte,
          form = Acc
        )
      }
    }

    // PUSH/POP r64
    for (r <- 0 until 8) {
      t(0x50 + r) = DecodeInfo(PUSH, hasModRM = false, dir = 1, form = Special)
      t(0x58 + r) = DecodeInfo(POP, hasModRM = false, dir = 1, form = Special)
    }

    t(0x63) = DecodeInfo(MOVSXD, hasModRM = true, dir = 1, form = DecodeForm.Movsxd)
    t(0x68) = DecodeInfo(PUSH, hasModRM = false, imm = ImmZ, dir = 1, form = Special)
    t(0x69) = DecodeInfo(IMUL, hasModRM = true, imm = ImmZ, dir = 1)
    t(0x6a) = DecodeInfo(PUSH, hasModRM = false, imm = Imm8, dir = 1, form = Special)
    t(0x6b) = DecodeInfo(IMUL, hasModRM = true, imm = Imm8, dir = 1)

    for (r <- 0 until 16) t(0x70 + r) = DecodeInfo(JCC, hasModRM = false, imm = Imm8)

    t(0x80) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp1, force8 = true, imm = Imm8)
    t(0x81) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp1, imm = ImmZ)
    t(0x83) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp1, imm = Imm8)

    t(0x84) = DecodeInfo(TEST, hasModRM = true, dir = 0, force8 = true)
    t(0x85) = DecodeInfo(TEST, hasModRM = true, dir = 0)
    t(0x86) = DecodeInfo(XCHG, hasModRM = true, dir = 0, force8 = true)
    t(0x87) = DecodeInfo(XCHG, hasModRM = true, dir = 0)

    t(0x88) = DecodeInfo(MOV, hasModRM = true, dir = 0, force8 = true)
    t(0x89) = DecodeInfo(MOV, hasModRM = true, dir = 0)
    t(0x8a) = DecodeInfo(MOV, hasModRM = true, dir = 1, force8 = true)
    t(0x8b) = DecodeInfo(MOV, hasModRM = true, dir = 1)
    t(0x8d) = DecodeInfo(LEA, hasModRM = true, dir = 1)
    t(0x8f) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp1A)

    t(0x90) = nop
    // 0x91-0x97 XCHG rAX, r
    for (r <- 1 until 8) t(0x90 + r) = DecodeInfo(XCHG, hasModRM = false, dir = 1, form = Special)

    t(0x98) = DecodeInfo(CBW, hasModRM = false)
    t(0x99) = DecodeInfo(CDQ, hasModRM = false)

    t(0x9c) = DecodeInfo(PUSHF, hasModRM = false)
    t(0x9d) = DecodeInfo(POPF, hasModRM = false)
    t(0x9e) = DecodeInfo(SAHF, hasModRM = false)
    t(0x9f) = DecodeInfo(LAHF, hasModRM = false)

    // String operations: operand size selects element width at decode time
    t(0xa4) = DecodeInfo(MOVS, hasModRM = false, force8 = true)
    t(0xa5) = DecodeInfo(MOVS, hasModRM = false)
    t(0xa6) = DecodeInfo(CMPS, hasModRM = false, force8 = true)
    t(0xa7) = DecodeInfo(CMPS, hasModRM = false)
    t(0xa8) = DecodeInfo(TEST, hasModRM = false, imm = Imm8, force8 = true, form = Acc)
    t(0xa9) = DecodeInfo(TEST, hasModRM = false, imm = ImmZ, form = Acc)
    t(0xaa) = DecodeInfo(STOS, hasModRM = false, force8 = true)
    t(0xab) = DecodeInfo(STOS, hasModRM = false)
    t(0xac) = DecodeInfo(LODS, hasModRM = false, force8 = true)
    t(0xad) = DecodeInfo(LODS, hasModRM = false)
    t(0xae) = DecodeInfo(SCAS, hasModRM = false, force8 = true)
    t(0xaf) = DecodeInfo(SCAS, hasModRM = false)

    // MOV r8, imm8
    for (r <- 0 until 8) t(0xb0 + r) = DecodeInfo(MOV, hasModRM = false, imm = Imm8, dir = 1, force8 = true, form = Special)
    // MOV r, imm
    for (r <- 0 until 8) t(0xb8 + r) = DecodeInfo(MOV, hasModRM = false, imm = Imm64, dir = 1, form = Special)

    t(0xc0) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp2, imm = Imm8, force8 = true)
    t(0xc1) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp2, imm = Imm8)
    t(0xc2) = DecodeInfo(RET, hasModRM = false, imm = Imm16)
    t(0xc3) = DecodeInfo(RET, hasModRM = false)
    t(0xc6) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp11, imm = Imm8, force8 = true)
    t(0xc7) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp11, imm = ImmZ)
    t(0xc8) = DecodeInfo(UNDEF, hasModRM = false, imm = Imm16) // ENTER unsupported
    t(0xc9) = DecodeInfo(UNDEF, hasModRM = false)             // LEAVE unsupported (decoded later)
    t(0xcc) = DecodeInfo(INT3, hasModRM = false)
    t(0xcd) = DecodeInfo(INT, hasModRM = false, imm = Imm8)
    t(0xcf) = DecodeInfo(IRET, hasModRM = false)

    t(0xd0) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp2, force8 = true)
    t(0xd1) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp2)
    t(0xd2) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp2, force8 = true)
    t(0xd3) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp2)
    t(0xd7) = DecodeInfo(UNDEF, hasModRM = false) // XLAT

    t(0xe0) = DecodeInfo(LOOPNE, hasModRM = false, imm = Imm8)
    t(0xe1) = DecodeInfo(LOOPE, hasModRM = false, imm = Imm8)
    t(0xe2) = DecodeInfo(LOOP, hasModRM = false, imm = Imm8)
    t(0xe3) = DecodeInfo(JRCXZ, hasModRM = false, imm = Imm8)

    t(0xe8) = DecodeInfo(CALL, hasModRM = false, imm = Imm32)
    t(0xe9) = DecodeInfo(JMP, hasModRM = false, imm = Imm32)
    t(0xeb) = DecodeInfo(JMP, hasModRM = false, imm = Imm8)

    t(0xf4) = DecodeInfo(HLT, hasModRM = false)
    t(0xf5) = DecodeInfo(CMC, hasModRM = false)
    t(0xf6) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp3, force8 = true)
    t(0xf7) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp3)
    t(0xf8) = DecodeInfo(CLC, hasModRM = false)
    t(0xf9) = DecodeInfo(STC, hasModRM = false)
    t(0xfa) = DecodeInfo(CLI, hasModRM = false)
    t(0xfb) = DecodeInfo(STI, hasModRM = false)
    t(0xfc) = DecodeInfo(CLD, hasModRM = false)
    t(0xfd) = DecodeInfo(STD, hasModRM = false)
    t(0xfe) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp4, force8 = true)
    t(0xff) = DecodeInfo(UNDEF, group = OpcodeGroup.Grp5)

    t
  }

  private def buildTwoByte(): Array[DecodeInfo] = {
    import Ops._
    val t = Array.fill(256)(undef)

    t(0x05) = DecodeInfo(SYSCALL, hasModRM = false)
    t(0x07) = DecodeInfo(SYSRET, hasModRM = false)
    t(0x20) = DecodeInfo(MOV_CR, hasModRM = true, dir = 0)
    t(0x22) = DecodeInfo(MOV_CR, hasModRM = true, dir = 1)
    t(0x30) = DecodeInfo(WRMSR, hasModRM = false)
    t(0x32) = DecodeInfo(RDMSR, hasModRM = false)
    t(0x0b) = DecodeInfo(UD2, hasModRM = false)
    t(0x01) = DecodeInfo(UNDEF, hasModRM = true)
    t(0x1e) = DecodeInfo(ENDBR, hasModRM = true)
    t(0x1f) = DecodeInfo(NOP, hasModRM = true)

    // SSE moves
    t(0x10) = DecodeInfo(MOVPS, hasModRM = true, dir = 1, sseSel = true)
    t(0x11) = DecodeInfo(MOVPS, hasModRM = true, dir = 0, store = true, sseSel = true)
    t(0x28) = DecodeInfo(MOVPS, hasModRM = true, dir = 1, sseSel = true)
    t(0x29) = DecodeInfo(MOVPS, hasModRM = true, dir = 0, store = true, sseSel = true)
    t(0x2a) = DecodeInfo(CVTSI2SD, hasModRM = true, dir = 1, sseSel = true)
    t(0x2c) = DecodeInfo(CVTTSD2SI, hasModRM = true, dir = 1, sseSel = true)
    t(0x2d) = DecodeInfo(CVTSD2SI, hasModRM = true, dir = 1, sseSel = true)
    t(0x2e) = DecodeInfo(SIMD_UCOMI, hasModRM = true, dir = 1, sseSel = true)
    t(0x2f) = DecodeInfo(SIMD_COMI, hasModRM = true, dir = 1, sseSel = true)

    for (r <- 0 until 16) t(0x40 + r) = DecodeInfo(CMOVCC, hasModRM = true, dir = 1)
    for (r <- 0 until 16) t(0x80 + r) = DecodeInfo(JCC, hasModRM = false, imm = Imm32)
    for (r <- 0 until 16) t(0x90 + r) = DecodeInfo(SETCC, hasModRM = true, force8 = true)

    t(0x54) = DecodeInfo(SIMD_AND, hasModRM = true, dir = 1, sseSel = true)
    t(0x55) = DecodeInfo(SIMD_ANDN, hasModRM = true, dir = 1, sseSel = true)
    t(0x56) = DecodeInfo(SIMD_OR, hasModRM = true, dir = 1, sseSel = true)
    t(0x57) = DecodeInfo(SIMD_XOR, hasModRM = true, dir = 1, sseSel = true)
    t(0x58) = DecodeInfo(SIMD_ADD, hasModRM = true, dir = 1, sseSel = true)
    t(0x59) = DecodeInfo(SIMD_MUL, hasModRM = true, dir = 1, sseSel = true)
    t(0x5a) = DecodeInfo(CVTSS2SD, hasModRM = true, dir = 1, sseSel = true)
    t(0x5b) = DecodeInfo(CVTDQ2PS, hasModRM = true, dir = 1)
    t(0x5c) = DecodeInfo(SIMD_SUB, hasModRM = true, dir = 1, sseSel = true)
    t(0x5d) = DecodeInfo(SIMD_MIN, hasModRM = true, dir = 1, sseSel = true)
    t(0x5e) = DecodeInfo(SIMD_DIV, hasModRM = true, dir = 1, sseSel = true)
    t(0x5f) = DecodeInfo(SIMD_MAX, hasModRM = true, dir = 1, sseSel = true)
    t(0x51) = DecodeInfo(SIMD_SQRT, hasModRM = true, dir = 1, sseSel = true)

    t(0x6e) = DecodeInfo(MOVD, hasModRM = true, dir = 1, sseSel = true)
    t(0x6f) = DecodeInfo(MOVDQ, hasModRM = true, dir = 1, sseSel = true)
    t(0x7e) = DecodeInfo(MOVD, hasModRM = true, dir = 0, store = true, sseSel = true)
    t(0x7f) = DecodeInfo(MOVDQ, hasModRM = true, dir = 0, store = true, sseSel = true)

    t(0xa2) = DecodeInfo(CPUID, hasModRM = false)
    t(0xaf) = DecodeInfo(IMUL, hasModRM = true, dir = 1)

    t(0xb6) = DecodeInfo(MOVZX, hasModRM = true, dir = 1, form = DecodeForm.Movzx8)
    t(0xb7) = DecodeInfo(MOVZX, hasModRM = true, dir = 1, form = DecodeForm.Movzx16)
    t(0xbe) = DecodeInfo(MOVSX, hasModRM = true, dir = 1, form = DecodeForm.Movsx8)
    t(0xbf) = DecodeInfo(MOVSX, hasModRM = true, dir = 1, form = DecodeForm.Movsx16)

    t(0xd6) = DecodeInfo(MOVQ, hasModRM = true, dir = 0, store = true, sseSel = true)
    t(0xc0) = DecodeInfo(XADD, hasModRM = true, dir = 0, force8 = true)
    t(0xc1) = DecodeInfo(XADD, hasModRM = true, dir = 0)

    t(0xe6) = DecodeInfo(CVTDQ2PD, hasModRM = true, dir = 1)

    t(0xdb) = DecodeInfo(SIMD_PAND, hasModRM = true, dir = 1)
    t(0xeb) = DecodeInfo(SIMD_POR, hasModRM = true, dir = 1)
    t(0xef) = DecodeInfo(SIMD_PXOR, hasModRM = true, dir = 1)
    t(0xfe) = DecodeInfo(SIMD_PADDD, hasModRM = true, dir = 1)
    t(0xfa) = DecodeInfo(SIMD_PSUBD, hasModRM = true, dir = 1)

    t
  }

  /** Group sub-tables, indexed by ModRM.reg (3 bits). */
  private def buildGroups(): Map[Int, Array[DecodeInfo]] = {
    import Ops._

    val grp1 = Array(ADD, OR, ADC, SBB, AND, SUB, XOR, CMP).map { o =>
      DecodeInfo(o, dir = 0)
    }
    val grp2 = Array(
      DecodeInfo(ROL),
      DecodeInfo(ROR),
      DecodeInfo(RCL),
      DecodeInfo(RCR),
      DecodeInfo(SHL),
      DecodeInfo(SHR),
      DecodeInfo(SHL), // /6 = SAL alias
      DecodeInfo(SAR)
    )
    val grp3 = Array(
      DecodeInfo(TEST, imm = ImmKind.ImmZ),
      DecodeInfo(TEST, imm = ImmKind.ImmZ),
      DecodeInfo(NOT),
      DecodeInfo(NEG),
      DecodeInfo(MUL),
      DecodeInfo(IMUL),
      DecodeInfo(DIV),
      DecodeInfo(IDIV)
    )
    val grp4 = Array(
      DecodeInfo(INC),
      DecodeInfo(DEC),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF)
    )
    val grp5 = Array(
      DecodeInfo(INC),
      DecodeInfo(DEC),
      DecodeInfo(CALL),
      DecodeInfo(CALL), // far call unsupported -> #UD at execution
      DecodeInfo(JMP),
      DecodeInfo(JMP), // far jmp unsupported
      DecodeInfo(PUSH),
      DecodeInfo(UNDEF)
    )
    val grp11 = Array(
      DecodeInfo(MOV),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF)
    )
    val grp1a = Array(
      DecodeInfo(POP),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF),
      DecodeInfo(UNDEF)
    )

    Map(
      OpcodeGroup.Grp1 -> grp1,
      OpcodeGroup.Grp2 -> grp2,
      OpcodeGroup.Grp3 -> grp3,
      OpcodeGroup.Grp4 -> grp4,
      OpcodeGroup.Grp5 -> grp5,
      OpcodeGroup.Grp11 -> grp11,
      OpcodeGroup.Grp1A -> grp1a
    )
  }

  val primary: Array[DecodeInfo] = buildPrimary()
  val twoByte: Array[DecodeInfo] = buildTwoByte()
  val groups: Map[Int, Array[DecodeInfo]] = buildGroups()

  val primaryPacked: Seq[BigInt] = primary.map(DecodeInfo.pack).toSeq
  val twoBytePacked: Seq[BigInt] = twoByte.map(DecodeInfo.pack).toSeq

  /** Flattened group ROM: index = (groupId-1)*8 + regField. */
  val groupPacked: Seq[BigInt] = (1 to 7).flatMap { g =>
    groups.getOrElse(g, Array.fill(8)(DecodeInfo(UNDEF))).map(DecodeInfo.pack)
  }.toSeq
}
