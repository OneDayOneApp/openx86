// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tongxiaojun

package x86.frontend

import chisel3._
import chisel3.util._
import x86.isa._

/**
 * x86-64 instruction decoder.
 *
 * Consumes a 16-byte fetch window starting at `pc` and produces one decoded
 * macro-op. The structure mirrors a classic x86 front-end: a prefix scanner,
 * an opcode ROM (one-byte and 0F maps), a ModRM/SIB/displacement parser, and
 * immediate extraction. The resulting [[Uop]] is microarchitecture independent
 * so the same decoder can feed the in-order pipeline and the future OoO core.
 */
class Decoder extends Module {
  private val FetchBytes = 16

  val io = IO(new Bundle {
    val pc    = Input(UInt(64.W))
    val bytes = Input(Vec(FetchBytes, UInt(8.W)))

    val uop     = Output(new Uop)
    val len     = Output(UInt(5.W))
    val illegal = Output(Bool())
  })

  // ---------------------------------------------------------------- prefixes
  private def isRexByte(b: UInt): Bool = (b >= 0x40.U) && (b <= 0x4f.U)
  private def isLegacyPrefix(b: UInt): Bool =
    b === 0x66.U || b === 0x67.U || b === 0xf0.U || b === 0xf2.U || b === 0xf3.U ||
      b === 0x2e.U || b === 0x36.U || b === 0x3e.U || b === 0x26.U ||
      b === 0x64.U || b === 0x65.U
  private def isPrefix(b: UInt): Bool = isLegacyPrefix(b) || isRexByte(b)

  private case class PState(
      off: UInt,
      p66: Bool,
      p67: Bool,
      rep: UInt,
      lock: Bool,
      rex: UInt,
      rexValid: Bool,
      stop: Bool,
      overflow: Bool
  )

  private def byteAt(o: UInt): UInt = io.bytes(o(3, 0))

  /** Table lookup accepting Int or UInt keys (Chisel's MuxLookup needs uniform keys). */
  private def lut(k: UInt, d: UInt, m: Seq[(Any, UInt)]): UInt =
    m.foldRight(d) { case ((key, v), acc) =>
      val kk: UInt = key match {
        case i: Int  => i.U
        case u: UInt => u
        case other   => throw new IllegalArgumentException(s"bad lut key: $other")
      }
      Mux(k === kk, v, acc)
    }

  private val pInit =
    PState(0.U(4.W), false.B, false.B, 0.U(2.W), false.B, 0.U(8.W), false.B, false.B, false.B)

  private val ps = (0 until 5).foldLeft(pInit) { (s, _) =>
    val b        = byteAt(s.off)
    val active   = !s.stop
    val isP      = isPrefix(b)
    val take     = active && isP
    val isRex    = isRexByte(b)
    val isLegacy = isLegacyPrefix(b)
    val atEnd    = s.off === 14.U && isP

    PState(
      off = Mux(take && !atEnd, (s.off + 1.U)(3, 0), s.off),
      p66 = Mux(take && b === 0x66.U, true.B, s.p66),
      p67 = Mux(take && b === 0x67.U, true.B, s.p67),
      rep = Mux(take && b === 0xf3.U, 1.U, Mux(take && b === 0xf2.U, 2.U, s.rep)),
      lock = Mux(take && b === 0xf0.U, true.B, s.lock),
      rex = Mux(take && isRex, b, Mux(take && isLegacy, 0.U, s.rex)),
      rexValid = Mux(take && isRex, true.B, Mux(take && isLegacy, false.B, s.rexValid)),
      stop = s.stop || (active && !isP) || atEnd,
      overflow = s.overflow || (atEnd && !s.stop)
    )
  }

  private val rex       = ps.rex
  private val rexW      = rex(3)
  private val rexR      = rex(2)
  private val rexX      = rex(1)
  private val rexB      = rex(0)
  private val rexActive = ps.rexValid

  private val p66 = ps.p66
  private val p67 = ps.p67
  private val rep = ps.rep

  // ------------------------------------------------------------------ opcode
  private val opOff      = ps.off
  private val b0         = byteAt(opOff)
  private val is0F       = b0 === 0x0f.U
  private val op1        = byteAt(opOff + 1.U)
  private val is3ByteMap = is0F && (op1 === 0x38.U || op1 === 0x3a.U)

  private val opcode      = Mux(is0F, op1, b0)
  private val opcodeLen   = Mux(is0F, 2.U, 1.U)
  private val afterOpcode = opOff + opcodeLen

  // -------------------------------------------------------------------- ROM
  private val primaryRom = VecInit(DecodeTables.primaryPacked.map(_.U(DecodeInfo.packedWidth.W)))
  private val twoByteRom = VecInit(DecodeTables.twoBytePacked.map(_.U(DecodeInfo.packedWidth.W)))
  private val groupRom   = VecInit(DecodeTables.groupPacked.map(_.U(DecodeInfo.packedWidth.W)))

  private val baseInfo = Mux(is0F, twoByteRom(opcode), primaryRom(opcode))

  private val baseModRm  = baseInfo(8)
  private val baseImm    = baseInfo(11, 9)
  private val baseDir    = baseInfo(12)
  private val baseGroup  = baseInfo(15, 13)
  private val baseForce8 = baseInfo(16)
  private val baseForm   = baseInfo(19, 17)
  private val baseSseSel = baseInfo(23)

  private val modrmByte = byteAt(afterOpcode)
  private val mod       = modrmByte(7, 6)
  private val regField  = modrmByte(5, 3)
  private val rmField   = modrmByte(2, 0)

  private val grpIdx = Mux(baseGroup === 0.U, regField.pad(6), Cat(baseGroup - 1.U, regField))
  private val grpInfo = groupRom(grpIdx)

  private val hasModRm = baseModRm && !is3ByteMap
  private val gActive  = hasModRm && (baseGroup =/= 0.U)

  private def pickGrp(sel: UInt, base: UInt): UInt = Mux(gActive, sel, base)

  // Group entries may leave the immediate unspecified (e.g. 0x80 uses the base imm8).
  private val grpImm   = grpInfo(11, 9)
  private val immKind  = Mux(gActive && grpImm =/= 0.U, grpImm, baseImm)
  private val dir     = pickGrp(grpInfo(12), baseDir)
  private val form    = pickGrp(grpInfo(19, 17), baseForm)

  private val opTable: UInt = Mux(
    is3ByteMap,
    Ops.UNDEF.U,
    Mux(gActive, grpInfo(7, 0), baseInfo(7, 0))
  )

  // Opcode-shared SSE encodings selected by the mandatory prefix.
  private val isE6 = is0F && opcode === 0xe6.U && !is3ByteMap
  private val is5A = is0F && opcode === 0x5a.U && !is3ByteMap
  private val is2A = is0F && opcode === 0x2a.U && !is3ByteMap
  private val is2C = is0F && opcode === 0x2c.U && !is3ByteMap
  private val is2D = is0F && opcode === 0x2d.U && !is3ByteMap
  private val is5B = is0F && opcode === 0x5b.U && !is3ByteMap
  private val isLidt = is0F && opcode === 0x01.U && !is3ByteMap && (regField === 3.U)
  private val isInvlpg = is0F && opcode === 0x01.U && !is3ByteMap && (regField === 7.U)

  private val op: UInt = MuxCase(opTable, Seq(
    isE6 -> Mux(p66, Ops.CVTTPD2DQ.U,
      Mux(rep === 1.U, Ops.CVTDQ2PD.U, Mux(rep === 2.U, Ops.CVTPD2DQ.U, Ops.UNDEF.U))),
    is5A -> Mux(rep === 1.U, Ops.CVTSS2SD.U, Mux(rep === 2.U, Ops.CVTSD2SS.U, Ops.UNDEF.U)),
    is2A -> Mux(rep === 1.U, Ops.CVTSI2SS.U, Mux(rep === 2.U, Ops.CVTSI2SD.U, Ops.UNDEF.U)),
    is2C -> Mux(rep === 1.U, Ops.CVTTSS2SI.U, Mux(rep === 2.U, Ops.CVTTSD2SI.U, Ops.UNDEF.U)),
    is2D -> Mux(rep === 2.U, Ops.CVTSD2SI.U, Ops.UNDEF.U),
    is5B -> Mux(p66, Ops.CVTPS2DQ.U, Ops.CVTDQ2PS.U),
    isLidt -> Ops.LIDT.U,
    isInvlpg -> Ops.INVLPG.U
  ))

  private val force8 = baseForce8
  private val sseSel = baseSseSel && !gActive

  // --------------------------------------------------------------- ModRM/SIB
  private val rmIsReg   = mod === 3.U
  private val hasSib    = hasModRm && !rmIsReg && rmField === 4.U
  private val sibByte   = byteAt(afterOpcode + 1.U)
  private val sibBase   = sibByte(2, 0)
  private val sibIdx    = sibByte(5, 3)
  private val sibScale  = sibByte(7, 6)

  private val dispOff = afterOpcode + Mux(hasModRm, 1.U, 0.U) + hasSib.asUInt

  private val dispLen = Mux(
    !hasModRm || rmIsReg,
    0.U,
    Mux(
      mod === 1.U,
      1.U,
      Mux(
        mod === 2.U,
        4.U,
        Mux(rmField === 5.U && !hasSib, 4.U, Mux(hasSib && sibBase === 5.U, 4.U, 0.U))
      )
    )
  )

  private val disp8  = byteAt(dispOff)
  private val disp32 = Cat(byteAt(dispOff + 3.U), byteAt(dispOff + 2.U), byteAt(dispOff + 1.U), byteAt(dispOff))

  private val disp64 = Mux(
    dispLen === 1.U,
    disp8.asSInt.pad(64).asUInt,
    Mux(dispLen === 4.U, disp32.asSInt.pad(64).asUInt, 0.U(64.W))
  )

  // -------------------------------------------------------------- operand size
  private val opszBytes: UInt =
    Mux(force8, 1.U(5.W), Mux(rexW, 8.U(5.W), Mux(p66, 2.U(5.W), 4.U(5.W))))

  // --------------------------------------------------------------- immediates
  private val immOff = dispOff + dispLen

  private val effImmKind: UInt = Mux(force8 && immKind === ImmKind.ImmZ.U, ImmKind.Imm8.U, immKind)

  private val immLen: UInt = lut(effImmKind, 0.U(4.W), 
    Seq(
      ImmKind.None -> 0.U,
      ImmKind.Imm8 -> 1.U,
      ImmKind.ImmZ -> Mux(opszBytes === 2.U, 2.U, 4.U),
      ImmKind.Imm32 -> 4.U,
      ImmKind.Imm64 -> Mux(opszBytes === 2.U, 2.U, Mux(rexW, 8.U, 4.U)),
      ImmKind.Imm16 -> 2.U
    )
  )

  private def immByte(i: Int): UInt = byteAt(immOff + i.U)

  private val immRaw = Cat(
    immByte(7),
    immByte(6),
    immByte(5),
    immByte(4),
    immByte(3),
    immByte(2),
    immByte(1),
    immByte(0)
  )

  private val imm8: UInt     = immRaw(7, 0)
  private val imm16v: UInt    = immRaw(15, 0)
  private val imm32v: UInt    = immRaw(31, 0)

  private val imm64: UInt = lut(effImmKind, 0.U(64.W), 
    Seq(
      ImmKind.None -> 0.U,
      ImmKind.Imm8 -> imm8.asSInt.pad(64).asUInt,
      ImmKind.Imm16 -> imm16v.pad(64),
      ImmKind.Imm32 -> imm32v.asSInt.pad(64).asUInt,
      ImmKind.ImmZ ->
        Mux(opszBytes === 2.U, imm16v.asSInt.pad(64).asUInt, imm32v.asSInt.pad(64).asUInt),
      ImmKind.Imm64 -> Mux(opszBytes === 2.U, imm16v.pad(64), Mux(rexW, immRaw, imm32v.pad(64)))
    )
  )

  private val instLen = immOff + immLen

  // ---------------------------------------------------------- prefix selector
  private val mand: UInt = Mux(rep === 1.U, 2.U, Mux(rep === 2.U, 3.U, Mux(p66, 1.U, 0.U)))

  private val isXmmOp: Bool = op === Ops.MOVD.U || op === Ops.MOVQ.U || op === Ops.MOVDQ.U ||
    op === Ops.MOVPS.U || (op >= Ops.SIMD_ADD.U && op <= Ops.CVTTPD2DQ.U) || op === Ops.SIMD_PAND.U ||
    op === Ops.SIMD_POR.U || op === Ops.SIMD_PXOR.U || op === Ops.SIMD_PADDD.U || op === Ops.SIMD_PSUBD.U

  private def simdArith(k: UInt): UInt = lut(
    k,
    SimdKind.PS.U,
    Seq(0 -> SimdKind.PS.U, 1 -> SimdKind.PD.U, 2 -> SimdKind.SS.U, 3 -> SimdKind.SD.U)
  )

  private val simdSel: UInt = lut(op, SimdKind.None.U, 
    Seq(
      Ops.MOVPS -> simdArith(mand),
      Ops.SIMD_ADD -> simdArith(mand),
      Ops.SIMD_SUB -> simdArith(mand),
      Ops.SIMD_MUL -> simdArith(mand),
      Ops.SIMD_DIV -> simdArith(mand),
      Ops.SIMD_SQRT -> simdArith(mand),
      Ops.SIMD_MIN -> simdArith(mand),
      Ops.SIMD_MAX -> simdArith(mand),
      Ops.SIMD_UCOMI -> Mux(p66, SimdKind.SD.U, SimdKind.SS.U),
      Ops.SIMD_COMI -> Mux(p66, SimdKind.SD.U, SimdKind.SS.U),
      Ops.MOVDQ -> SimdKind.DQ.U,
      Ops.MOVD -> Mux(rexW, SimdKind.SD.U, SimdKind.SS.U),
      Ops.MOVQ -> SimdKind.SD.U,
      Ops.CVTSI2SD -> SimdKind.SD.U,
      Ops.CVTSI2SS -> SimdKind.SS.U,
      Ops.CVTTSD2SI -> SimdKind.SD.U,
      Ops.CVTTSS2SI -> SimdKind.SS.U,
      Ops.CVTSS2SD -> SimdKind.SS.U,
      Ops.CVTSD2SS -> SimdKind.SD.U
    )
  )

  private def simdMemBytes(k: UInt): UInt = lut(k, 16.U, 
    Seq(SimdKind.SS.U -> 4.U, SimdKind.SD.U -> 8.U, SimdKind.PS.U -> 16.U, SimdKind.PD.U -> 16.U,
      SimdKind.DQ.U -> 16.U)
  )

  private val memBytes: UInt = lut(op, opszBytes, 
    Seq(
      Ops.MOVPS -> simdMemBytes(simdSel),
      Ops.MOVDQ -> 16.U,
      Ops.MOVD -> Mux(rexW, 8.U, 4.U),
      Ops.MOVQ -> 8.U,
      Ops.SIMD_ADD -> simdMemBytes(simdSel),
      Ops.SIMD_SUB -> simdMemBytes(simdSel),
      Ops.SIMD_MUL -> simdMemBytes(simdSel),
      Ops.SIMD_DIV -> simdMemBytes(simdSel),
      Ops.SIMD_SQRT -> simdMemBytes(simdSel),
      Ops.SIMD_MIN -> simdMemBytes(simdSel),
      Ops.SIMD_MAX -> simdMemBytes(simdSel),
      Ops.SIMD_UCOMI -> simdMemBytes(simdSel),
      Ops.SIMD_COMI -> simdMemBytes(simdSel),
      Ops.SIMD_PAND -> 16.U,
      Ops.SIMD_POR -> 16.U,
      Ops.SIMD_PXOR -> 16.U,
      Ops.SIMD_PADDD -> 16.U,
      Ops.SIMD_PSUBD -> 16.U,
      Ops.CVTSI2SD -> opszBytes,
      Ops.CVTSI2SS -> opszBytes,
      Ops.CVTTSS2SI -> 4.U,
      Ops.CVTSD2SI -> 8.U,
      Ops.CVTSS2SD -> 4.U,
      Ops.CVTSD2SS -> 8.U,
      Ops.MOVZX -> Mux(form === DecodeForm.Movzx8.U, 1.U, 2.U),
      Ops.MOVSX -> Mux(form === DecodeForm.Movsx8.U, 1.U, 2.U),
      Ops.MOVSXD -> 4.U,
      Ops.SETCC -> 1.U,
      Ops.LEA -> 8.U,
      Ops.CALL -> 8.U,
      Ops.JMP -> 8.U,
      Ops.PUSH -> 8.U,
      Ops.POP -> 8.U
    )
  )

  // -------------------------------------------------------------- operand kinds
  private val rmKind: UInt = Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)

  private val aRegBase = Mux(dir === 1.U, regField | Mux(rexR, 8.U, 0.U), rmField | Mux(rexB, 8.U, 0.U))
  private val bRegBase = Mux(dir === 1.U, rmField | Mux(rexB, 8.U, 0.U), regField | Mux(rexR, 8.U, 0.U))

  private val accForm    = form === DecodeForm.Acc.U
  private val specialFrm = form === DecodeForm.Special.U
  private val opLow3     = opcode(2, 0)

  private val uop = Wire(new Uop)
  uop := 0.U.asTypeOf(new Uop)

  uop.pc   := io.pc
  uop.len  := instLen(3, 0)
  uop.op   := op
  uop.imm  := imm64
  private val cvtToGpr = op === Ops.CVTTSD2SI.U || op === Ops.CVTTSS2SI.U || op === Ops.CVTSD2SI.U
  private val cvtFromGpr = op === Ops.CVTSI2SS.U || op === Ops.CVTSI2SD.U
  uop.simd := simdSel
  uop.opsz := Mux(isXmmOp,
    Mux(op === Ops.MOVD.U, Mux(rexW, 8.U, 4.U),
      Mux(op === Ops.MOVQ.U, 8.U,
        Mux(cvtToGpr || cvtFromGpr, Mux(rexW, 8.U, 4.U), 16.U))),
    opszBytes)
  uop.lock := ps.lock
  uop.rep  := Mux(isXmmOp, 0.U, ps.rep)
  uop.cond := opcode(3, 0)
  uop.valid := true.B

  // dir=1: ModRM.reg is the destination (a) and r/m is the source (b).
  // dir=0: r/m is the destination (a) and ModRM.reg is the source (b).
  uop.a.kind := Mux(dir === 1.U, OperandKind.Gpr.U,
    Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U))
  uop.a.idx  := aRegBase
  uop.b.kind := Mux(dir === 1.U,
    Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U), OperandKind.Gpr.U)
  uop.b.idx  := bRegBase

  uop.memsz := memBytes
  uop.mem.base     := Mux(hasSib, sibBase | Mux(rexB, 8.U, 0.U), rmField | Mux(rexB, 8.U, 0.U))
  uop.mem.index    := sibIdx | Mux(rexX, 8.U, 0.U)
  uop.mem.scale    := sibScale
  uop.mem.disp     := disp64
  uop.mem.hasBase  := hasModRm && !rmIsReg && !(mod === 0.U && rmField === 5.U && !hasSib) &&
    !(hasSib && mod === 0.U && sibBase === 5.U)
  uop.mem.hasIndex := hasSib && !(sibIdx === 4.U && !rexX)
  uop.mem.ripRel   := hasModRm && !rmIsReg && !hasSib && mod === 0.U && rmField === 5.U
  uop.mem.addr32   := p67

  // Accumulator-immediate forms (04/05, A8/A9, ...)
  when(accForm) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 0.U
    uop.b.kind := OperandKind.Imm.U
  }

  // Register-only special forms: PUSH/POP r64, MOV r, imm, XCHG rAX, r
  when(specialFrm && op =/= Ops.XCHG.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := opLow3 | Mux(rexB, 8.U, 0.U)
    uop.b.kind := Mux(immKind =/= 0.U, OperandKind.Imm.U, OperandKind.None.U)
  }

  when(specialFrm && op === Ops.XCHG.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 0.U
    uop.b.kind := OperandKind.Gpr.U
    uop.b.idx  := opLow3 | Mux(rexB, 8.U, 0.U)
  }

  // Arithmetic/logic group with immediate (Grp1)
  when(gActive && baseGroup === OpcodeGroup.Grp1.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.Imm.U
  }

  // MOV r/m, imm (Grp11)
  when(gActive && baseGroup === OpcodeGroup.Grp11.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.Imm.U
  }

  // Grp3: TEST (imm), NOT/NEG (unary), MUL/DIV (implicit rAX/rDX)
  when(gActive && baseGroup === OpcodeGroup.Grp3.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := Mux(
      regField === 0.U || regField === 1.U,
      OperandKind.Imm.U,
      OperandKind.None.U
    )
  }

  // Grp4/Grp5: inc/dec/call/jmp/push from r/m
  when(gActive && (baseGroup === OpcodeGroup.Grp4.U || baseGroup === OpcodeGroup.Grp5.U)) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.None.U
  }

  // Grp1A: POP r/m
  when(gActive && baseGroup === OpcodeGroup.Grp1A.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.None.U
  }

  // Shifts: count from imm8, constant 1, or CL
  when(gActive && baseGroup === OpcodeGroup.Grp2.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    when(opcode === 0xd2.U || opcode === 0xd3.U) {
      uop.b.kind := OperandKind.Gpr.U
      uop.b.idx  := 1.U
    }.elsewhen(opcode === 0xd0.U || opcode === 0xd1.U) {
      uop.b.kind := OperandKind.Imm.U
      uop.imm    := 1.U
    }.otherwise {
      uop.b.kind := OperandKind.Imm.U
    }
  }

  when(op === Ops.SETCC.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.None.U
  }

  when(op === Ops.CMOVCC.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := regField | Mux(rexR, 8.U, 0.U)
    uop.b.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.b.idx  := rmField | Mux(rexB, 8.U, 0.U)
  }

  when(op === Ops.MOVZX.U || op === Ops.MOVSX.U || op === Ops.MOVSXD.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := regField | Mux(rexR, 8.U, 0.U)
    uop.b.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    uop.b.idx  := rmField | Mux(rexB, 8.U, 0.U)
  }

  when(op === Ops.LEA.U) {
    uop.b.kind := OperandKind.Mem.U
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := regField | Mux(rexR, 8.U, 0.U)
  }

  // Control transfers with relative immediates
  when(op === Ops.JCC.U || op === Ops.LOOP.U) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := OperandKind.Imm.U
  }

  when(!gActive && (op === Ops.JMP.U || op === Ops.CALL.U)) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := OperandKind.Imm.U
  }

  // PUSH imm
  when(op === Ops.PUSH.U && immKind =/= 0.U) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := OperandKind.Imm.U
  }

  when(op === Ops.RET.U) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := Mux(immKind =/= 0.U, OperandKind.Imm.U, OperandKind.None.U)
  }

  when(op === Ops.LAHF.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 0.U
    uop.b.kind := OperandKind.None.U
    uop.opsz   := 2.U
  }

  when(op === Ops.SAHF.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 0.U
    uop.b.kind := OperandKind.None.U
  }

  when(op === Ops.LOOP.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 1.U
    uop.b.kind := OperandKind.None.U
    uop.opsz   := 8.U
  }

  // In 64-bit mode PUSH/POP default to 64-bit operands (66 -> 16-bit).
  when(op === Ops.PUSH.U || op === Ops.POP.U) {
    uop.opsz := Mux(p66, 2.U, 8.U)
  }

  when(op === Ops.LOOPNE.U || op === Ops.LOOPE.U || op === Ops.JRCXZ.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 1.U
    uop.b.kind := OperandKind.None.U
    uop.opsz   := 8.U
  }

  when(op === Ops.INT.U) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := OperandKind.None.U
  }

  when(op === Ops.NOP.U || op === Ops.ENDBR.U) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := OperandKind.None.U
    uop.memsz  := 0.U
  }

  when(op === Ops.HLT.U || op === Ops.SYSCALL.U || op === Ops.CPUID.U ||
    op === Ops.UD2.U || op === Ops.INT3.U || op === Ops.INT.U ||
    op === Ops.PUSHF.U || op === Ops.POPF.U ||
    op === Ops.CLC.U || op === Ops.STC.U || op === Ops.CMC.U || op === Ops.CLD.U ||
    op === Ops.STD.U || op === Ops.CLI.U || op === Ops.STI.U ||
    op === Ops.CBW.U || op === Ops.CDQ.U) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := OperandKind.None.U
  }

  // String operations: implicit rSI/rDI/rCX operands
  when(op === Ops.MOVS.U || op === Ops.STOS.U || op === Ops.LODS.U || op === Ops.SCAS.U || op === Ops.CMPS.U) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := OperandKind.None.U
  }

  // SSE operand rules
  when(isXmmOp && dir === 1.U && op =/= Ops.CVTTSD2SI.U && op =/= Ops.CVTTSS2SI.U && op =/= Ops.CVTSD2SI.U) {
    uop.a.kind := OperandKind.Xmm.U
    uop.a.idx  := regField | Mux(rexR, 8.U, 0.U)
    uop.b.kind := Mux(rmIsReg, OperandKind.Xmm.U, OperandKind.Mem.U)
    uop.b.idx  := rmField | Mux(rexB, 8.U, 0.U)
    when(op === Ops.CVTSI2SD.U || op === Ops.CVTSI2SS.U) {
      uop.b.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
    }
  }

  when(isXmmOp && dir === 0.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Xmm.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.Xmm.U
    uop.b.idx  := regField | Mux(rexR, 8.U, 0.U)
  }

  // MOVD 7E stores to a GPR or memory
  when(op === Ops.MOVD.U && dir === 0.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
  }

  // MOVD/MOVQ 6E/7E sources
  when(op === Ops.MOVD.U && dir === 1.U) {
    uop.b.kind := Mux(rmIsReg, OperandKind.Gpr.U, OperandKind.Mem.U)
  }

  when(op === Ops.CVTTSD2SI.U || op === Ops.CVTTSS2SI.U || op === Ops.CVTSD2SI.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := regField | Mux(rexR, 8.U, 0.U)
    uop.b.kind := Mux(rmIsReg, OperandKind.Xmm.U, OperandKind.Mem.U)
    uop.b.idx  := rmField | Mux(rexB, 8.U, 0.U)
  }

  when(op === Ops.LIDT.U) {
    uop.a.kind := Mux(rmIsReg, OperandKind.None.U, OperandKind.Mem.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.None.U
    uop.memsz  := 10.U
  }

  when(op === Ops.INVLPG.U) {
    uop.a.kind := OperandKind.Mem.U
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.None.U
    uop.memsz  := 1.U
  }

  when(op === Ops.CPUID.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 0.U
    uop.b.kind := OperandKind.None.U
  }

  when(op === Ops.MOV_CR.U) {
    uop.a.kind := Mux(dir === 1.U, OperandKind.None.U, OperandKind.Gpr.U)
    uop.a.idx  := rmField | Mux(rexB, 8.U, 0.U)
    uop.b.kind := OperandKind.None.U
    uop.imm    := regField.pad(64)
    uop.cond := Cat(0.U(3.W), dir === 1.U)
  }

  when(op === Ops.RDMSR.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 1.U
    uop.b.kind := OperandKind.None.U
  }
  when(op === Ops.WRMSR.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 1.U
    uop.b.kind := OperandKind.Gpr.U
    uop.b.idx  := 0.U
  }
  when(op === Ops.SYSRET.U) {
    uop.a.kind := OperandKind.Gpr.U
    uop.a.idx  := 1.U
    uop.b.kind := OperandKind.Gpr.U
    uop.b.idx  := 11.U
  }
  when(op === Ops.SYSCALL.U || op === Ops.IRET.U) {
    uop.a.kind := OperandKind.None.U
    uop.b.kind := OperandKind.None.U
  }

  // ---------------------------------------------------------------- legality
  private val prefixLen = opOff
  private val badSsePrefix = isXmmOp && (op === Ops.SIMD_PAND.U || op === Ops.SIMD_POR.U ||
    op === Ops.SIMD_PXOR.U || op === Ops.SIMD_PADDD.U || op === Ops.SIMD_PSUBD.U ||
    op === Ops.MOVDQ.U || op === Ops.MOVQ.U) && !(p66 || rep === 1.U)

  uop.illegal := is3ByteMap || ps.overflow || (instLen > 15.U) || (op === Ops.UNDEF.U) ||
    (op === Ops.MOVSXD.U && !rexW) ||
    ((op === Ops.MOV_CR.U) && !rmIsReg) ||
    ((op === Ops.IRET.U) && !rexW) || badSsePrefix

  io.uop     := uop
  io.len     := instLen
  io.illegal := uop.illegal
}
