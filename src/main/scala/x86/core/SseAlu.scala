// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tongxiaojun

package x86.core

import chisel3._
import chisel3.util._
import x86.isa._

/**
 * SSE/SSE2 execution unit.
 *
 * This first version covers 128-bit moves, bitwise operations, packed integer
 * add/sub and the COMISS/COMISD family. Floating-point arithmetic is provided
 * by [[FpUnit]] and added through the same interface.
 */
class SseAlu extends Module {
  val io = IO(new Bundle {
    val op    = Input(UInt(8.W))
    val simd  = Input(UInt(3.W))
    val opsz  = Input(UInt(5.W))
    val a     = Input(UInt(128.W))
    val b     = Input(UInt(128.W))
    val imm   = Input(UInt(8.W))
    val flagsIn = Input(UInt(64.W))

    val result = Output(UInt(128.W))
    val gprResult = Output(UInt(64.W))
    val flags  = Output(UInt(64.W))
    val valid  = Output(Bool())
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

  private def lane32(x: UInt, i: Int): UInt = x(32 * i + 31, 32 * i)
  private def lane64(x: UInt, i: Int): UInt = x(64 * i + 63, 64 * i)

  private def isNan32(x: UInt): Bool = (x(30, 23) === "hff".U) && (x(22, 0) =/= 0.U)
  private def isNan64(x: UInt): Bool = (x(62, 52) === "h7ff".U) && (x(51, 0) =/= 0.U)

  private def cmp32(a: UInt, b: UInt): (Bool, Bool) = {
    val aS = a(31); val bS = b(31)
    val aMag = a(30, 0); val bMag = b(30, 0)
    val eq = (a === b) || (aMag === 0.U && bMag === 0.U)
    val lt = Mux(aS =/= bS, aS && !(aMag === 0.U && bMag === 0.U),
      Mux(aS, aMag > bMag, aMag < bMag))
    (eq, lt)
  }

  private def cmp64(a: UInt, b: UInt): (Bool, Bool) = {
    val aS = a(63); val bS = b(63)
    val aMag = a(62, 0); val bMag = b(62, 0)
    val eq = (a === b) || (aMag === 0.U && bMag === 0.U)
    val lt = Mux(aS =/= bS, aS && !(aMag === 0.U && bMag === 0.U),
      Mux(aS, aMag > bMag, aMag < bMag))
    (eq, lt)
  }


  private val a0 = lane64(io.a, 0)
  private val b0 = lane64(io.b, 0)
  private val a32 = lane32(io.a, 0)
  private val b32 = lane32(io.b, 0)

  private def fpBin(op: UInt, x: UInt, y: UInt, sub: Bool): UInt =
    lut(op, Fp.addSub(x, y, sub, 8, 23), 
      Seq(
        Ops.SIMD_ADD -> Fp.addSub(x, y, sub, 8, 23),
        Ops.SIMD_SUB -> Fp.addSub(x, y, true.B, 8, 23),
        Ops.SIMD_MUL -> Fp.mul(x, y, 8, 23),
        Ops.SIMD_DIV -> Fp.div(x, y, 8, 23)
      )
    )

  private def fpBin64(op: UInt, x: UInt, y: UInt, sub: Bool): UInt =
    lut(op, Fp.addSub(x, y, sub, 11, 52), 
      Seq(
        Ops.SIMD_ADD -> Fp.addSub(x, y, sub, 11, 52),
        Ops.SIMD_SUB -> Fp.addSub(x, y, true.B, 11, 52),
        Ops.SIMD_MUL -> Fp.mul(x, y, 11, 52),
        Ops.SIMD_DIV -> Fp.div(x, y, 11, 52)
      )
    )

  private def arithResult: UInt = {
    val sub = io.op === Ops.SIMD_SUB.U
    val r0 = fpBin(io.op, a32, b32, sub)
    val r1 = fpBin(io.op, lane32(io.a, 1), lane32(io.b, 1), sub)
    val r2 = fpBin(io.op, lane32(io.a, 2), lane32(io.b, 2), sub)
    val r3 = fpBin(io.op, lane32(io.a, 3), lane32(io.b, 3), sub)
    val d0 = fpBin64(io.op, a0, b0, sub)
    val d1 = fpBin64(io.op, lane64(io.a, 1), lane64(io.b, 1), sub)
    val ps = Cat(r3, r2, r1, r0)
    val pd = Cat(d1, d0)
    lut(io.simd, ps, 
      Seq(
        SimdKind.SS -> Cat(io.a(127, 32), r0),
        SimdKind.SD -> Cat(io.a(127, 64), d0),
        SimdKind.PS -> ps,
        SimdKind.PD -> pd
      )
    )
  }

  private def sqrtResult: UInt = {
    val s0 = Fp.sqrt(b32, 8, 23)
    val s1 = Fp.sqrt(lane32(io.b, 1), 8, 23)
    val s2 = Fp.sqrt(lane32(io.b, 2), 8, 23)
    val s3 = Fp.sqrt(lane32(io.b, 3), 8, 23)
    val d0 = Fp.sqrt(b0, 11, 52)
    val d1 = Fp.sqrt(lane64(io.b, 1), 11, 52)
    lut(io.simd, Cat(s3, s2, s1, s0),
      Seq(
        SimdKind.SS -> Cat(io.a(127, 32), s0),
        SimdKind.SD -> Cat(io.a(127, 64), d0),
        SimdKind.PS -> Cat(s3, s2, s1, s0),
        SimdKind.PD -> Cat(d1, d0)
      ))
  }

  private def minMaxResult: UInt = {
    val isMin = io.op === Ops.SIMD_MIN.U
    val r0 = Fp.minMax(a32, b32, isMin, 8, 23)
    val r1 = Fp.minMax(lane32(io.a, 1), lane32(io.b, 1), isMin, 8, 23)
    val r2 = Fp.minMax(lane32(io.a, 2), lane32(io.b, 2), isMin, 8, 23)
    val r3 = Fp.minMax(lane32(io.a, 3), lane32(io.b, 3), isMin, 8, 23)
    val d0 = Fp.minMax(a0, b0, isMin, 11, 52)
    val d1 = Fp.minMax(lane64(io.a, 1), lane64(io.b, 1), isMin, 11, 52)
    lut(io.simd, Cat(r3, r2, r1, r0), 
      Seq(
        SimdKind.SS -> Cat(io.a(127, 32), r0),
        SimdKind.SD -> Cat(io.a(127, 64), d0),
        SimdKind.PS -> Cat(r3, r2, r1, r0),
        SimdKind.PD -> Cat(d1, d0)
      )
    )
  }

  private val intSrc32 = io.b(31, 0)
  private val intSrc64 = io.b(63, 0)
  private val intSrc = Mux(io.opsz === 8.U, intSrc64,
    Cat(Fill(32, io.b(31)), io.b(31, 0)))
  private val cvtF32 = Fp.intToFloat(intSrc, true.B, 8, 23)
  private val cvtF64 = Fp.intToFloat(intSrc, true.B, 11, 52)

  private val tr32 = Fp.floatToIntTrunc(b32, 8, 23, 32)
  private val tr64 = Fp.floatToIntTrunc(b32, 8, 23, 64)
  private val trd32 = Fp.floatToIntTrunc(b0, 11, 52, 32)
  private val trd64 = Fp.floatToIntTrunc(b0, 11, 52, 64)
  private val bad32 = "h80000000".U(32.W)
  private val bad64 = "h800000000000000".U(64.W)

  private val res = WireDefault(0.U(128.W))
  private val flags = WireDefault(0.U(64.W))
  private val valid = WireDefault(true.B)
  private val gpr = WireDefault(0.U(64.W))

  private val cmpFlagMask = ((BigInt(1) << FlagBit.CF) | (BigInt(1) << FlagBit.PF) |
    (BigInt(1) << FlagBit.AF) | (BigInt(1) << FlagBit.ZF) |
    (BigInt(1) << FlagBit.SF) | (BigInt(1) << FlagBit.OF)).U(64.W)

  private def setCmpFlags(unordered: Bool, eq: Bool, lt: Bool): UInt =
    (io.flagsIn & ~cmpFlagMask) |
      (Mux(unordered || lt, 1.U(64.W), 0.U(64.W)) << FlagBit.CF) |
      (Mux(unordered || eq, 1.U(64.W), 0.U(64.W)) << FlagBit.ZF) |
      (unordered.asUInt << FlagBit.PF)

  switch(io.op) {
    is(Ops.MOVD.U) {
      res := io.b
      gpr := Mux(io.opsz === 8.U, io.b(63, 0), io.b(31, 0).pad(64))
    }
    is(Ops.MOVQ.U) {
      res := io.b
      gpr := io.b(63, 0)
    }
    is(Ops.MOVDQ.U) { res := io.b }
    is(Ops.MOVPS.U) { res := io.b }
    is(Ops.SIMD_AND.U) { res := io.a & io.b }
    is(Ops.SIMD_ANDN.U) { res := io.a & ~io.b }
    is(Ops.SIMD_OR.U) { res := io.a | io.b }
    is(Ops.SIMD_XOR.U) { res := io.a ^ io.b }
    is(Ops.SIMD_PAND.U) { res := io.a & io.b }
    is(Ops.SIMD_POR.U) { res := io.a | io.b }
    is(Ops.SIMD_PXOR.U) { res := io.a ^ io.b }
    is(Ops.SIMD_PADDD.U) {
      val r = Wire(Vec(4, UInt(32.W)))
      for (i <- 0 until 4) r(i) := lane32(io.a, i) + lane32(io.b, i)
      res := Cat(r(3), r(2), r(1), r(0))
    }
    is(Ops.SIMD_PSUBD.U) {
      val r = Wire(Vec(4, UInt(32.W)))
      for (i <- 0 until 4) r(i) := lane32(io.a, i) - lane32(io.b, i)
      res := Cat(r(3), r(2), r(1), r(0))
    }
    is(Ops.SIMD_UCOMI.U) {
      when(io.simd === SimdKind.SD.U) {
        val un = isNan64(a0) || isNan64(b0)
        val (eq, lt) = cmp64(a0, b0)
        flags := setCmpFlags(un, eq, lt)
        res := io.a
      }.otherwise {
        val un = isNan32(a32) || isNan32(b32)
        val (eq, lt) = cmp32(a32, b32)
        flags := setCmpFlags(un, eq, lt)
        res := io.a
      }
    }
    is(Ops.SIMD_COMI.U) {
      when(io.simd === SimdKind.SD.U) {
        val un = isNan64(a0) || isNan64(b0)
        val (eq, lt) = cmp64(a0, b0)
        flags := setCmpFlags(un, eq, lt)
        res := io.a
      }.otherwise {
        val un = isNan32(a32) || isNan32(b32)
        val (eq, lt) = cmp32(a32, b32)
        flags := setCmpFlags(un, eq, lt)
        res := io.a
      }
    }
    is(Ops.SIMD_ADD.U) { res := arithResult }
    is(Ops.SIMD_SUB.U) { res := arithResult }
    is(Ops.SIMD_MUL.U) { res := arithResult }
    is(Ops.SIMD_DIV.U) { res := arithResult }
    is(Ops.SIMD_SQRT.U) { res := sqrtResult }
    is(Ops.SIMD_MIN.U) { res := minMaxResult }
    is(Ops.SIMD_MAX.U) { res := minMaxResult }
    is(Ops.CVTSI2SD.U) { res := Cat(io.a(127, 64), cvtF64) }
    is(Ops.CVTSI2SS.U) { res := Cat(io.a(127, 32), cvtF32) }
    is(Ops.CVTTSD2SI.U) {
      gpr := Mux(io.opsz === 8.U, Mux(trd64._2, trd64._1, bad64), Mux(trd32._2, trd32._1, bad32).pad(64))
      res := io.a
    }
    is(Ops.CVTTSS2SI.U) {
      gpr := Mux(io.opsz === 8.U, Mux(tr64._2, tr64._1, bad64), Mux(tr32._2, tr32._1, bad32).pad(64))
      res := io.a
    }
    is(Ops.CVTSS2SD.U) { res := Cat(io.a(127, 64), Fp.f32ToF64(b32)) }
    is(Ops.CVTSD2SS.U) { res := Cat(io.a(127, 32), Fp.f64ToF32(b0)) }
    is(Ops.CVTDQ2PS.U) {
      val f0 = Fp.intToFloat(lane32(io.b, 0), true.B, 8, 23)
      val f1 = Fp.intToFloat(lane32(io.b, 1), true.B, 8, 23)
      val f2 = Fp.intToFloat(lane32(io.b, 2), true.B, 8, 23)
      val f3 = Fp.intToFloat(lane32(io.b, 3), true.B, 8, 23)
      res := Cat(f3, f2, f1, f0)
    }
    is(Ops.CVTPS2DQ.U) {
      val i0 = Fp.floatToIntTrunc(lane32(io.b, 0), 8, 23, 32)._1
      val i1 = Fp.floatToIntTrunc(lane32(io.b, 1), 8, 23, 32)._1
      val i2 = Fp.floatToIntTrunc(lane32(io.b, 2), 8, 23, 32)._1
      val i3 = Fp.floatToIntTrunc(lane32(io.b, 3), 8, 23, 32)._1
      res := Cat(i3, i2, i1, i0)
    }
    is(Ops.CVTDQ2PD.U) {
      val d0 = Fp.intToFloat(lane32(io.b, 0), true.B, 11, 52)
      val d1 = Fp.intToFloat(lane32(io.b, 1), true.B, 11, 52)
      res := Cat(d1, d0)
    }
    is(Ops.CVTPD2DQ.U) {
      val i0 = Fp.floatToIntTrunc(b0, 11, 52, 32)._1
      val i1 = Fp.floatToIntTrunc(lane64(io.b, 1), 11, 52, 32)._1
      res := Cat(0.U(64.W), i1, i0)
    }
    is(Ops.CVTTPD2DQ.U) {
      val i0 = Fp.floatToIntTrunc(b0, 11, 52, 32)._1
      val i1 = Fp.floatToIntTrunc(lane64(io.b, 1), 11, 52, 32)._1
      res := Cat(0.U(64.W), i1, i0)
    }
  }

  io.result := res
  io.gprResult := gpr
  io.flags  := flags
  io.valid  := valid
}
