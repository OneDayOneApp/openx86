package x86.core

import chisel3._
import chisel3.util._

/** Simple valid/ready memory interfaces used by the Stage-A in-order core. */
class IMemReq extends Bundle {
  val valid = Bool()
  val addr  = UInt(64.W)
}

class IMemResp extends Bundle {
  val valid = Bool()
  val data  = UInt(256.W) // 32-byte fetch window
}

class DMemReq extends Bundle {
  val valid = Bool()
  val we    = Bool()
  val addr  = UInt(64.W)
  val wdata = UInt(128.W)
  val wmask = UInt(16.W)
  val size  = UInt(5.W)
}

class DMemResp extends Bundle {
  val valid = Bool()
  val rdata = UInt(128.W)
}

class PtwReq extends Bundle {
  val valid = Bool()
  val addr  = UInt(64.W)
}

class PtwResp extends Bundle {
  val valid = Bool()
  val data  = UInt(64.W)
}

/** Retire trace consumed by the difftest harness. */
class CommitTrace extends Bundle {
  val valid = Bool()
  val pc    = UInt(64.W)
  val len   = UInt(4.W)
  val rd    = UInt(5.W)
  val wdata = UInt(64.W)
  val wreg  = Bool()
  val halted = Bool()
  val exitCode = UInt(64.W)
  val flags = UInt(64.W)
  val writesFlags = Bool()
  val op = UInt(8.W)
}
