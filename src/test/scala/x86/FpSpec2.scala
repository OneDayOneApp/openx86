// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tongxiaojun

package x86

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec
import x86.core.Fp

class FpDiv64 extends Module {
  val io = IO(new Bundle {
    val a = Input(UInt(64.W)); val b = Input(UInt(64.W)); val out = Output(UInt(64.W))
  })
  io.out := Fp.div(io.a, io.b, 11, 52)
}

class FpI2F64 extends Module {
  val io = IO(new Bundle { val x = Input(SInt(64.W)); val out = Output(UInt(64.W)) })
  io.out := Fp.intToFloat(io.x.asUInt, true.B, 11, 52)
}

class FpF2I64 extends Module {
  val io = IO(new Bundle {
    val x = Input(UInt(64.W)); val out = Output(UInt(64.W)); val ok = Output(Bool())
  })
  val (v, ok) = Fp.floatToIntTrunc(io.x, 11, 52, 64)
  io.out := v; io.ok := ok
}

class FpSqrt64 extends Module {
  val io = IO(new Bundle { val x = Input(UInt(64.W)); val out = Output(UInt(64.W)) })
  io.out := Fp.sqrt(io.x, 11, 52)
}

class FpSqrt32 extends Module {
  val io = IO(new Bundle { val x = Input(UInt(32.W)); val out = Output(UInt(32.W)) })
  io.out := Fp.sqrt(io.x, 8, 23)
}

class FpConv32to64 extends Module {
  val io = IO(new Bundle { val x = Input(UInt(32.W)); val out = Output(UInt(64.W)) })
  io.out := Fp.f32ToF64(io.x)
}

class FpConv64to32 extends Module {
  val io = IO(new Bundle { val x = Input(UInt(64.W)); val out = Output(UInt(32.W)) })
  io.out := Fp.f64ToF32(io.x)
}

class FpSpec2 extends AnyFlatSpec with ChiselScalatestTester {
  behavior of "soft float extras"

  it should "divide doubles" in {
    test(new FpDiv64) { c =>
      c.io.a.poke("h401e000000000000".U) // 7.5
      c.io.b.poke("h4004000000000000".U) // 2.5
      c.io.out.expect("h4008000000000000".U) // 3.0
    }
  }

  it should "convert int to double" in {
    test(new FpI2F64) { c =>
      c.io.x.poke(1234.S)
      c.io.out.expect("h4093480000000000".U)
    }
  }

  it should "convert double to int" in {
    test(new FpF2I64) { c =>
      c.io.x.poke("h400e000000000000".U) // 3.75
      c.io.ok.expect(true.B)
      c.io.out.expect(3.U)
    }
  }

  it should "widen float to double" in {
    test(new FpConv32to64) { c =>
      c.io.x.poke("h3fc00000".U)
      c.io.out.expect("h3ff8000000000000".U)
    }
  }

  it should "narrow double to float" in {
    test(new FpConv64to32) { c =>
      c.io.x.poke("h3ff8000000000000".U)
      c.io.out.expect("h3fc00000".U)
    }
  }

  it should "sqrt double" in {
    test(new FpSqrt64) { c =>
      c.io.x.poke("h4010000000000000".U) // 4.0
      c.io.out.expect("h4000000000000000".U) // 2.0
    }
  }

  it should "sqrt two" in {
    test(new FpSqrt64) { c =>
      c.io.x.poke("h4000000000000000".U) // 2.0
      c.io.out.expect("h3ff6a09e667f3bcd".U)
    }
  }

  it should "sqrt float" in {
    test(new FpSqrt32) { c =>
      c.io.x.poke("h40800000".U) // 4.0f
      c.io.out.expect("h40000000".U)
    }
  }

  it should "narrow pi" in {
    test(new FpConv64to32) { c =>
      c.io.x.poke("h400921fb54442d18".U)
      c.io.out.expect("h40490fdb".U)
    }
  }
}
