package x86

import chisel3._
import x86.core.X86Core

/** Top-level wrapper exposing the Stage-A core. */
class X86Top extends Module {
  val io = IO(new Bundle {
    val imem = new Bundle {
      val req  = Output(new x86.core.IMemReq)
      val resp = Input(new x86.core.IMemResp)
    }
    val dmem = new Bundle {
      val req  = Output(new x86.core.DMemReq)
      val resp = Input(new x86.core.DMemResp)
    }
    val ptw = new Bundle {
      val req  = Output(new x86.core.PtwReq)
      val resp = Input(new x86.core.PtwResp)
    }
    val halted    = Output(Bool())
    val exitCode  = Output(UInt(64.W))
    val fatal     = Output(Bool())
    val fatalPc   = Output(UInt(64.W))
    val fatalCode = Output(UInt(8.W))
    val commit    = Output(new x86.core.CommitTrace)
    val dbg = Output(new Bundle {
      val gpr   = Vec(16, UInt(64.W))
      val rip   = UInt(64.W)
      val flags = UInt(64.W)
      val cycles = UInt(64.W)
      val exValid = Bool()
      val exPc = UInt(64.W)
      val exOp = UInt(8.W)
      val exAIdx = UInt(5.W)
      val exA = UInt(64.W)
      val exB = UInt(64.W)
      val aluA = UInt(64.W)
      val aluB = UInt(64.W)
      val xmm = Vec(16, UInt(128.W))
      val sysValid = Bool()
      val sysPc = UInt(64.W)
      val sysOp = UInt(8.W)
      val sysTrap = Bool()
      val sysVec = UInt(8.W)
      val sysErr = UInt(32.W)
      val trapValid = Bool()
      val trapVec = UInt(8.W)
      val trapErr = UInt(32.W)
      val trapPc = UInt(64.W)
    })
  })

  val core = Module(new X86Core)
  io.imem <> core.io.imem
  io.dmem <> core.io.dmem
  io.ptw <> core.io.ptw
  io.halted    := core.io.halted
  io.exitCode  := core.io.exitCode
  io.fatal     := core.io.fatal
  io.fatalPc   := core.io.fatalPc
  io.fatalCode := core.io.fatalCode
  io.commit    := core.io.commit
  io.dbg       := core.io.dbg
}
