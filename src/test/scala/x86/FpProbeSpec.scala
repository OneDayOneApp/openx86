// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tongxiaojun

package x86

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec
import x86.core.Fp

class FpProbe32 extends Module {
  val io = IO(new Bundle {
    val a = Input(UInt(32.W))
    val b = Input(UInt(32.W))
    val out = Output(UInt(32.W))
    val mag0 = Output(UInt(27.W))
    val norm = Output(UInt(27.W))
    val expN = Output(UInt(8.W))
  })
  val (o, m, n, e) = Fp.addSubDebug(io.a, io.b, false.B, 8, 23)
  io.mag0 := m
  io.norm := n
  io.expN := e
  io.out := o
}

class FpProbeSpec extends AnyFlatSpec with ChiselScalatestTester {
  "probe" should "print" in {
    test(new FpProbe32) { c =>
      c.io.a.poke("h3fc00000".U) // 1.5
      c.io.b.poke("h40200000".U) // 2.5
      println(f"mag0=${c.io.mag0.peek().litValue.toLong}%x norm=${c.io.norm.peek().litValue.toLong}%x expN=${c.io.expN.peek().litValue.toLong}%x out=${c.io.out.peek().litValue.toLong}%x")
    }
  }
}
