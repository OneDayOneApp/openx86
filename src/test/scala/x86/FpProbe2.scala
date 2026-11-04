package x86

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec
import x86.core.Fp

class FpProbeF2I extends Module {
  val io = IO(new Bundle {
    val x = Input(UInt(64.W))
    val out = Output(UInt(64.W))
    val ok = Output(Bool())
    val e = Output(UInt(11.W))
    val geZero = Output(Bool())
    val tooBig = Output(Bool())
    val nan = Output(Bool())
    val inf = Output(Bool())
  })
  val e = io.x(62, 52)
  val f = io.x(51, 0)
  io.e := e
  io.nan := (e === 2047.U) && (f =/= 0.U)
  io.inf := (e === 2047.U) && (f === 0.U)
  io.geZero := e >= 1023.U
  io.tooBig := e >= (1023 + 63).U
  val (v, ok) = Fp.floatToIntTrunc(io.x, 11, 52, 64)
  io.out := v; io.ok := ok
}

class FpProbe2 extends AnyFlatSpec with ChiselScalatestTester {
  "f2i probe" should "print" in {
    test(new FpProbeF2I) { c =>
      c.io.x.poke("h400e000000000000".U)
      println(f"e=${c.io.e.peek().litValue} geZero=${c.io.geZero.peek().litValue} tooBig=${c.io.tooBig.peek().litValue} nan=${c.io.nan.peek().litValue} inf=${c.io.inf.peek().litValue} ok=${c.io.ok.peek().litValue} out=${c.io.out.peek().litValue}")
    }
  }
}
