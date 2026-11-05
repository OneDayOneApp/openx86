// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tongxiaojun

package x86.core

import chisel3._
import chisel3.util._

/** 16 x 64-bit GPR file with two read ports and two write ports. */
class GprFile extends Module {
  val io = IO(new Bundle {
    val rs1     = Input(UInt(5.W))
    val rs1Data = Output(UInt(64.W))
    val rs2     = Input(UInt(5.W))
    val rs2Data = Output(UInt(64.W))
    val rs3     = Input(UInt(5.W))
    val rs3Data = Output(UInt(64.W))
    val rs4     = Input(UInt(5.W))
    val rs4Data = Output(UInt(64.W))
    val rs5     = Input(UInt(5.W))
    val rs5Data = Output(UInt(64.W))
    val rs6     = Input(UInt(5.W))
    val rs6Data = Output(UInt(64.W))
    val rs7     = Input(UInt(5.W))
    val rs7Data = Output(UInt(64.W))

    val we1    = Input(Bool())
    val waddr1 = Input(UInt(5.W))
    val wdata1 = Input(UInt(64.W))
    val we2    = Input(Bool())
    val waddr2 = Input(UInt(5.W))
    val wdata2 = Input(UInt(64.W))

    val dbg = Output(Vec(16, UInt(64.W)))
  })

  val regs = RegInit(VecInit(Seq.fill(16)(0.U(64.W))))
  io.rs1Data := regs(io.rs1)
  io.rs2Data := regs(io.rs2)
  io.rs3Data := regs(io.rs3)
  io.rs4Data := regs(io.rs4)
  io.rs5Data := regs(io.rs5)
  io.rs6Data := regs(io.rs6)
  io.rs7Data := regs(io.rs7)

  for (i <- 0 until 16) { io.dbg(i) := regs(i) }

  when(io.we1) { regs(io.waddr1) := io.wdata1 }
  when(io.we2) { regs(io.waddr2) := io.wdata2 }
}

/** 16 x 128-bit XMM register file. */
class XmmFile extends Module {
  val io = IO(new Bundle {
    val rs1     = Input(UInt(5.W))
    val rs1Data = Output(UInt(128.W))
    val rs2     = Input(UInt(5.W))
    val rs2Data = Output(UInt(128.W))

    val we     = Input(Bool())
    val waddr  = Input(UInt(5.W))
    val wdata  = Input(UInt(128.W))

    val dbg = Output(Vec(16, UInt(128.W)))
  })

  val regs = RegInit(VecInit(Seq.fill(16)(0.U(128.W))))
  io.rs1Data := regs(io.rs1)
  io.rs2Data := regs(io.rs2)
  for (i <- 0 until 16) { io.dbg(i) := regs(i) }
  when(io.we) { regs(io.waddr) := io.wdata }
}
