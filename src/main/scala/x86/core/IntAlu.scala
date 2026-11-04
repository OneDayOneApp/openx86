package x86.core

import chisel3._
import chisel3.util._
import x86.isa._

/**
 * Integer ALU + branch condition evaluation.
 *
 * The ALU is combinational. Multi-result instructions (MUL/DIV/CDQ) expose a
 * second write port (`hi`) targeting RDX. All arithmetic is performed on the
 * masked operand width so 8/16/32-bit operations do not disturb upper bits.
 */
class IntAlu extends Module {
  val io = IO(new Bundle {
    val op       = Input(UInt(8.W))
    val opsz     = Input(UInt(5.W)) // bytes: 1,2,4,8
    val a        = Input(UInt(64.W)) // destination old value / dividend low
    val b        = Input(UInt(64.W)) // source value
    val hiIn     = Input(UInt(64.W)) // RDX (dividend high)
    val imm      = Input(UInt(64.W))
    val addrIn   = Input(UInt(64.W)) // LEA effective address
    val flagsIn  = Input(UInt(64.W))
    val cond     = Input(UInt(4.W))
    val oneOp    = Input(Bool()) // MUL/IMUL/DIV/IDIV one-operand form

    val result      = Output(UInt(64.W))
    val hi          = Output(UInt(64.W))
    val flags       = Output(UInt(64.W))
    val writeEnable = Output(Bool())
    val writeHi     = Output(Bool())
    val writesFlags = Output(Bool())
    val divError    = Output(Bool())
    val branchTaken = Output(Bool())
  })

  private def lut[T <: Data](k: UInt, d: T, m: Seq[(Any, T)]): T =
    m.foldRight(d) { case ((key, v), acc) =>
      val kk: UInt = key match {
        case i: Int  => i.U
        case u: UInt => u
        case other   => throw new IllegalArgumentException(s"bad lut key: $other")
      }
      Mux(k === kk, v, acc)
    }

  private val fullMask = "hffffffffffffffff".U(64.W)
  private val mask = lut(
    io.opsz,
    fullMask,
    Seq(1 -> "hff".U(64.W), 2 -> "hffff".U(64.W), 4 -> "hffffffff".U(64.W))
  )
  private val widthBits: UInt = io.opsz << 3
  private val signMask: UInt  = 1.U(64.W) << (widthBits - 1.U)

  private val aM = io.a & mask
  private val bM = io.b & mask
  private val cfIn = io.flagsIn(FlagBit.CF)

  private def signOf(x: UInt): Bool = (x & signMask) =/= 0.U

  private def parity(x: UInt): Bool = {
    val b = x(7, 0)
    !(b(0) ^ b(1) ^ b(2) ^ b(3) ^ b(4) ^ b(5) ^ b(6) ^ b(7))
  }

  private val flagMask = ((BigInt(1) << FlagBit.CF) | (BigInt(1) << FlagBit.PF) |
    (BigInt(1) << FlagBit.AF) | (BigInt(1) << FlagBit.ZF) |
    (BigInt(1) << FlagBit.SF) | (BigInt(1) << FlagBit.OF)).U(64.W)

  private def mkFlags(cf: Bool, of: Bool, af: Bool, sf: Bool, zf: Bool, pf: Bool): UInt = {
    (io.flagsIn & ~flagMask) |
      (cf.asUInt << FlagBit.CF) | (pf.asUInt << FlagBit.PF) |
      (af.asUInt << FlagBit.AF) | (zf.asUInt << FlagBit.ZF) |
      (sf.asUInt << FlagBit.SF) | (of.asUInt << FlagBit.OF)
  }

  private def condTrue(c: UInt, f: UInt): Bool = lut(
    c,
    false.B,
    Seq(
      0  -> f(FlagBit.OF),
      1  -> !f(FlagBit.OF),
      2  -> f(FlagBit.CF),
      3  -> !f(FlagBit.CF),
      4  -> f(FlagBit.ZF),
      5  -> !f(FlagBit.ZF),
      6  -> (f(FlagBit.CF) || f(FlagBit.ZF)),
      7  -> (!f(FlagBit.CF) && !f(FlagBit.ZF)),
      8  -> f(FlagBit.SF),
      9  -> !f(FlagBit.SF),
      10 -> f(FlagBit.PF),
      11 -> !f(FlagBit.PF),
      12 -> (f(FlagBit.SF) =/= f(FlagBit.OF)),
      13 -> (f(FlagBit.SF) === f(FlagBit.OF)),
      14 -> (f(FlagBit.ZF) || (f(FlagBit.SF) =/= f(FlagBit.OF))),
      15 -> (!f(FlagBit.ZF) && (f(FlagBit.SF) === f(FlagBit.OF)))
    )
  )

  // ---------------------------------------------------------------- arithmetic
  private val sumNoC = Cat(0.U(1.W), aM) + Cat(0.U(1.W), bM)
  private val sumC   = sumNoC + Cat(0.U(64.W), cfIn.asUInt)
  private val sum65  = Mux(io.op === Ops.ADC.U, sumC, sumNoC)

  private val subNoC = Cat(0.U(1.W), aM) - Cat(0.U(1.W), bM)
  private val subC   = subNoC - Cat(0.U(64.W), cfIn.asUInt)
  private val sub65  = Mux(io.op === Ops.SBB.U, subC, subNoC)

  private val resAdd = sum65(63, 0) & mask
  private val resSub = sub65(63, 0) & mask

  private val cfAdd = (sum65 >> widthBits)(0)
  private val cfSub = (sub65 >> widthBits)(0)

  private val ofAdd = (((~(aM ^ bM)) & (aM ^ resAdd) & signMask) =/= 0.U)
  private val ofSub = (((aM ^ bM) & (aM ^ resSub) & signMask) =/= 0.U)

  private val afAdd = (((aM ^ bM ^ resAdd) & 0x10.U) =/= 0.U)
  private val afSub = (((aM ^ bM ^ resSub) & 0x10.U) =/= 0.U)

  // ------------------------------------------------------------------- logic
  private val logicAnd = aM & bM
  private val logicOr  = aM | bM
  private val logicXor = aM ^ bM
  private val logicNot = ~aM & mask

  // ------------------------------------------------------------------ shifts
  private val count5 = Mux(widthBits === 64.U, bM(5, 0), bM(4, 0))
  private val countW = Mux(widthBits === 64.U, bM(5, 0), bM(4, 0)) // rotate modulo width
  private val rotAmt = countW & (widthBits - 1.U)
  private val shlRes = (aM << count5) & mask
  private val shrRes = (aM >> count5) & mask

  private val signFill = Mux((aM & signMask) =/= 0.U, ~mask, 0.U(64.W))
  private val aSigned  = (aM | signFill).asSInt
  private val sarRes   = ((aSigned >> count5).asUInt) & mask

  private val rolRes = ((aM << rotAmt) | (aM >> (widthBits - rotAmt))) & mask
  private val rorRes = ((aM >> rotAmt) | (aM << (widthBits - rotAmt))) & mask

  private val shiftCF = lut(
    io.op,
    false.B,
    Seq(
      Ops.SHL -> ((aM >> (widthBits - count5))(0) & count5.orR),
      Ops.SHR -> ((aM >> (count5 - 1.U))(0) & count5.orR),
      Ops.SAR -> ((aM >> (count5 - 1.U))(0) & count5.orR),
      Ops.ROL -> (rolRes(0)),
      Ops.ROR -> ((rorRes & signMask) =/= 0.U)
    )
  )

  // -------------------------------------------------------------------- mul
  private val mulU   = aM * bM
  private val aSign  = Mux((aM & signMask) =/= 0.U, Cat(Fill(64, 1.U), aM).asSInt, Cat(0.U(64.W), aM).asSInt)
  private val bSign  = Mux((bM & signMask) =/= 0.U, Cat(Fill(64, 1.U), bM).asSInt, Cat(0.U(64.W), bM).asSInt)
  private val mulS   = (aSign * bSign).asUInt
  private val prodLo = Mux(io.op === Ops.IMUL.U, mulS(63, 0), mulU(63, 0))
  private val prodHi = Mux(io.op === Ops.IMUL.U, mulS(127, 64), mulU(127, 64))

  // -------------------------------------------------------------------- div
  private val dividendU = Cat(io.hiIn & mask, aM)
  private val bDivU     = bM
  private val divByZero = bDivU === 0.U

  private def sext128(x: UInt, n: Int): SInt =
    if (n >= 128) x.asSInt else Cat(Fill(128 - n, x(n - 1)), x(n - 1, 0)).asSInt
  private val dividendS = Mux(widthBits === 64.U, sext128(dividendU, 128),
    Mux(widthBits === 32.U, sext128(dividendU, 64),
      Mux(widthBits === 16.U, sext128(dividendU, 32), sext128(dividendU, 16))))
  private val divisorS = Mux(widthBits === 64.U, sext128(bDivU, 64),
    Mux(widthBits === 32.U, sext128(bDivU, 32),
      Mux(widthBits === 16.U, sext128(bDivU, 16), sext128(bDivU, 8))))
  private val qU        = dividendU / bDivU
  private val rU        = dividendU % bDivU
  private val qS        = dividendS / divisorS
  private val rS        = dividendS % divisorS

  private val qOverflowU = (qU >> widthBits).orR
  private val qFitsS = Mux(widthBits === 64.U, sext128(qS(63, 0), 64) === qS,
    Mux(widthBits === 32.U, sext128(qS(31, 0), 32) === qS,
      Mux(widthBits === 16.U, sext128(qS(15, 0), 16) === qS, sext128(qS(7, 0), 8) === qS)))
  private val divActive  = io.op === Ops.DIV.U || io.op === Ops.IDIV.U
  private val divError   = divActive && (divByZero || Mux(io.op === Ops.IDIV.U, !qFitsS, qOverflowU))

  // ----------------------------------------------------------------- results
  private val res = WireDefault(0.U(64.W))
  private val hiOut = WireDefault(0.U(64.W))
  private val flagsOut = WireDefault(io.flagsIn)
  private val we = WireDefault(false.B)
  private val weHi = WireDefault(false.B)
  private val wf = WireDefault(false.B)
  private val branch = WireDefault(false.B)

  // OF for rotates is defined only when the count is 1.
  private val rotOf = (countW === 1.U) && (signOf(rolRes) ^ shiftCF)

  switch(io.op) {
    is(Ops.MOV.U) { res := bM }
    is(Ops.MOVZX.U) { res := bM }
    is(Ops.MOVSX.U) { res := bM }
    is(Ops.MOVSXD.U) { res := bM }
    is(Ops.LEA.U) { res := io.addrIn }

    is(Ops.ADD.U) {
      res := resAdd
      flagsOut := mkFlags(cfAdd, ofAdd, afAdd, signOf(resAdd), resAdd === 0.U, parity(resAdd))
      wf := true.B
    }
    is(Ops.ADC.U) {
      res := resAdd
      flagsOut := mkFlags(cfAdd, ofAdd, afAdd, signOf(resAdd), resAdd === 0.U, parity(resAdd))
      wf := true.B
    }
    is(Ops.SUB.U) {
      res := resSub
      flagsOut := mkFlags(cfSub, ofSub, afSub, signOf(resSub), resSub === 0.U, parity(resSub))
      wf := true.B
    }
    is(Ops.SBB.U) {
      res := resSub
      flagsOut := mkFlags(cfSub, ofSub, afSub, signOf(resSub), resSub === 0.U, parity(resSub))
      wf := true.B
    }
    is(Ops.CMP.U) {
      res := resSub
      flagsOut := mkFlags(cfSub, ofSub, afSub, signOf(resSub), resSub === 0.U, parity(resSub))
      wf := true.B
    }
    is(Ops.AND.U) {
      res := logicAnd
      flagsOut := mkFlags(false.B, false.B, false.B, signOf(logicAnd), logicAnd === 0.U, parity(logicAnd))
      wf := true.B
    }
    is(Ops.OR.U) {
      res := logicOr
      flagsOut := mkFlags(false.B, false.B, false.B, signOf(logicOr), logicOr === 0.U, parity(logicOr))
      wf := true.B
    }
    is(Ops.XOR.U) {
      res := logicXor
      flagsOut := mkFlags(false.B, false.B, false.B, signOf(logicXor), logicXor === 0.U, parity(logicXor))
      wf := true.B
    }
    is(Ops.TEST.U) {
      res := logicAnd
      flagsOut := mkFlags(false.B, false.B, false.B, signOf(logicAnd), logicAnd === 0.U, parity(logicAnd))
      wf := true.B
    }
    is(Ops.INC.U) {
      val r = ((aM + 1.U) & mask)
      res := r
      flagsOut := mkFlags(cfIn, ofAdd, afAdd, signOf(r), r === 0.U, parity(r))
      wf := true.B
    }
    is(Ops.DEC.U) {
      val r = ((aM - 1.U) & mask)
      res := r
      flagsOut := mkFlags(cfIn, ofSub, afSub, signOf(r), r === 0.U, parity(r))
      wf := true.B
    }
    is(Ops.NEG.U) {
      val r = ((0.U - aM) & mask)
      res := r
      flagsOut := mkFlags(aM =/= 0.U, aM === signMask,
        (aM & "hf".U) =/= 0.U, signOf(r), r === 0.U, parity(r))
      wf := true.B
    }
    is(Ops.NOT.U) { res := logicNot }
    is(Ops.XCHG.U) { res := bM }

    is(Ops.SHL.U) {
      res := shlRes
      flagsOut := Mux(count5 === 0.U, io.flagsIn,
        mkFlags(shiftCF, Mux(count5 === 1.U, signOf(res) =/= signOf(aM), false.B),
          false.B, signOf(shlRes), shlRes === 0.U, parity(shlRes)))
      wf := true.B
    }
    is(Ops.SHR.U) {
      res := shrRes
      flagsOut := Mux(count5 === 0.U, io.flagsIn,
        mkFlags(shiftCF, Mux(count5 === 1.U, signOf(aM), false.B),
          false.B, signOf(shrRes), shrRes === 0.U, parity(shrRes)))
      wf := true.B
    }
    is(Ops.SAR.U) {
      res := sarRes
      flagsOut := Mux(count5 === 0.U, io.flagsIn,
        mkFlags(shiftCF, false.B, false.B, signOf(sarRes), sarRes === 0.U, parity(sarRes)))
      wf := true.B
    }
    is(Ops.ROL.U) {
      res := rolRes
      // Rotates only define CF (and OF for count 1); ZF/SF/PF/AF are preserved.
      flagsOut := Mux(count5 === 0.U, io.flagsIn,
        (io.flagsIn & ~((1.U << FlagBit.CF) | (1.U << FlagBit.OF))) |
          (shiftCF << FlagBit.CF) | (rotOf << FlagBit.OF))
      wf := true.B
    }
    is(Ops.ROR.U) {
      res := rorRes
      flagsOut := Mux(count5 === 0.U, io.flagsIn,
        (io.flagsIn & ~((1.U << FlagBit.CF) | (1.U << FlagBit.OF))) |
          (shiftCF << FlagBit.CF) | (rotOf << FlagBit.OF))
      wf := true.B
    }
    is(Ops.RCL.U) {
      res := rolRes
      flagsOut := Mux(count5 === 0.U, io.flagsIn,
        (io.flagsIn & ~((1.U << FlagBit.CF) | (1.U << FlagBit.OF))) |
          (shiftCF << FlagBit.CF) | (rotOf << FlagBit.OF))
      wf := true.B
    }
    is(Ops.RCR.U) {
      res := rorRes
      flagsOut := Mux(count5 === 0.U, io.flagsIn,
        (io.flagsIn & ~((1.U << FlagBit.CF) | (1.U << FlagBit.OF))) |
          (shiftCF << FlagBit.CF) | (rotOf << FlagBit.OF))
      wf := true.B
    }

    is(Ops.MUL.U) {
      res := prodLo
      hiOut := prodHi
      weHi := true.B
      val ovf = prodHi =/= 0.U
      flagsOut := (io.flagsIn & ~((1.U << FlagBit.CF) | (1.U << FlagBit.OF))) |
        (ovf << FlagBit.CF) | (ovf << FlagBit.OF)
      wf := true.B
    }
    is(Ops.IMUL.U) {
      res := prodLo
      when(io.oneOp) {
        hiOut := prodHi
        weHi := true.B
        val fits = (mulS(127, 64) === Fill(64, mulS(63)))
        flagsOut := (io.flagsIn & ~((1.U << FlagBit.CF) | (1.U << FlagBit.OF))) |
          ((!fits) << FlagBit.CF) | ((!fits) << FlagBit.OF)
      }.otherwise {
        val fits2 = Mux(widthBits === 64.U, mulS(127, 64) === Fill(64, prodLo(63)),
          Mux(widthBits === 32.U, mulS(127, 32) === Fill(96, prodLo(31)),
            Mux(widthBits === 16.U, mulS(127, 16) === Fill(112, prodLo(15)),
              mulS(127, 8) === Fill(120, prodLo(7)))))
        flagsOut := (io.flagsIn & ~((1.U << FlagBit.CF) | (1.U << FlagBit.OF))) |
          ((!fits2) << FlagBit.CF) | ((!fits2) << FlagBit.OF)
      }
      wf := true.B
    }

    is(Ops.DIV.U) {
      res := Mux(divError, 0.U, qU(63, 0) & mask)
      hiOut := Mux(divError, 0.U, rU(63, 0) & mask)
      weHi := true.B
      wf := true.B
    }
    is(Ops.IDIV.U) {
      res := Mux(divError, 0.U, qS(63, 0).asUInt & mask)
      hiOut := Mux(divError, 0.U, rS(63, 0).asUInt & mask)
      weHi := true.B
      wf := true.B
    }

    is(Ops.SETCC.U) {
      res := condTrue(io.cond, io.flagsIn)
    }
    is(Ops.CMOVCC.U) {
      res := Mux(condTrue(io.cond, io.flagsIn), bM, aM)
    }

    is(Ops.CDQ.U) {
      val sb = (aM >> (widthBits - 1.U))(0)
      hiOut := Mux(sb, mask, 0.U)
      weHi := true.B
      res := aM
    }
    is(Ops.CBW.U) {
      val srcBits = widthBits >> 1
      val sb = (aM >> (srcBits - 1.U))(0)
      val ext = Mux(sb, ~((1.U(64.W) << srcBits) - 1.U), 0.U(64.W))
      res := (aM & ((1.U(64.W) << srcBits) - 1.U)) | ext
    }
    is(Ops.JCC.U) { branch := condTrue(io.cond, io.flagsIn) }
    is(Ops.LOOP.U) { res := aM - 1.U }
    is(Ops.LOOPNE.U) { res := aM - 1.U }
    is(Ops.LOOPE.U) { res := aM - 1.U }
    is(Ops.JRCXZ.U) { res := aM }
    is(Ops.LAHF.U) {
      res := Cat(io.flagsIn(FlagBit.SF), io.flagsIn(FlagBit.ZF), 0.U(1.W),
        io.flagsIn(FlagBit.AF), 0.U(1.W), io.flagsIn(FlagBit.PF), 1.U(1.W),
        io.flagsIn(FlagBit.CF), aM(7, 0))
    }
  }

  private val writesA = io.op =/= Ops.CMP.U && io.op =/= Ops.TEST.U &&
    io.op =/= Ops.JCC.U && io.op =/= Ops.LOOP.U &&
    io.op =/= Ops.PUSH.U && io.op =/= Ops.POP.U &&
    io.op =/= Ops.JMP.U && io.op =/= Ops.CALL.U && io.op =/= Ops.RET.U &&
    io.op =/= Ops.HLT.U && io.op =/= Ops.NOP.U &&
    io.op =/= Ops.PUSHF.U && io.op =/= Ops.POPF.U && io.op =/= Ops.LAHF.U && io.op =/= Ops.SAHF.U &&
    io.op =/= Ops.CLC.U && io.op =/= Ops.STC.U && io.op =/= Ops.CMC.U &&
    io.op =/= Ops.CLD.U && io.op =/= Ops.STD.U && io.op =/= Ops.CLI.U && io.op =/= Ops.STI.U &&
    io.op =/= Ops.CDQ.U &&
    io.op =/= Ops.MOVS.U && io.op =/= Ops.STOS.U && io.op =/= Ops.LODS.U &&
    io.op =/= Ops.SCAS.U && io.op =/= Ops.CMPS.U &&
    io.op =/= Ops.HLT.U && io.op =/= Ops.SYSCALL.U && io.op =/= Ops.CPUID.U &&
    io.op =/= Ops.UD2.U && io.op =/= Ops.INT.U && io.op =/= Ops.INT3.U

  io.result      := res
  io.hi          := hiOut
  io.flags       := flagsOut
  io.writeEnable := writesA
  io.writeHi     := weHi
  io.writesFlags := wf
  io.divError    := divError
  io.branchTaken := branch
}
