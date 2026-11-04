package x86

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec
import x86.core.Fp

class FpAdd64 extends Module {
  val io = IO(new Bundle {
    val a = Input(UInt(64.W))
    val b = Input(UInt(64.W))
    val sub = Input(Bool())
    val out = Output(UInt(64.W))
  })
  io.out := Fp.addSub(io.a, io.b, io.sub, 11, 52)
}

class FpMul64 extends Module {
  val io = IO(new Bundle {
    val a = Input(UInt(64.W))
    val b = Input(UInt(64.W))
    val out = Output(UInt(64.W))
  })
  io.out := Fp.mul(io.a, io.b, 11, 52)
}

class FpAdd32 extends Module {
  val io = IO(new Bundle {
    val a = Input(UInt(32.W))
    val b = Input(UInt(32.W))
    val out = Output(UInt(32.W))
  })
  io.out := Fp.addSub(io.a, io.b, false.B, 8, 23)
}

class FpSpec extends AnyFlatSpec with ChiselScalatestTester {
  behavior of "soft float"

  it should "add doubles" in {
    test(new FpAdd64) { c =>
      c.io.a.poke("h3ff8000000000000".U)
      c.io.b.poke("h4002000000000000".U)
      c.io.sub.poke(false.B)
      c.io.out.expect("h400e000000000000".U)
    }
  }

  it should "add floats" in {
    test(new FpAdd32) { c =>
      c.io.a.poke("h3fc00000".U)
      c.io.b.poke("h40200000".U)
      c.io.out.expect("h40800000".U)
    }
  }

  it should "multiply doubles" in {
    test(new FpMul64) { c =>
      c.io.a.poke("h4008000000000000".U) // 3.0
      c.io.b.poke("h4000000000000000".U) // 2.0
      c.io.out.expect("h4018000000000000".U) // 6.0
    }
  }
}
