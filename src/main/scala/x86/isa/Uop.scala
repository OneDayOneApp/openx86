package x86.isa

import chisel3._
import chisel3.util._

/** Identifier of an architectural operand register (kind + index). */
class Operand extends Bundle {
  val kind = UInt(3.W)
  val idx  = UInt(5.W)
}

object Operand {
  def none: Operand = {
    val o = Wire(new Operand)
    o.kind := OperandKind.None.U
    o.idx  := 0.U
    o
  }
}

/** Memory address description: base + index*scale + disp, or RIP-relative. */
class MemRef extends Bundle {
  val base     = UInt(5.W)
  val index    = UInt(5.W)
  val scale    = UInt(2.W)
  val disp     = UInt(64.W)
  val hasBase  = Bool()
  val hasIndex = Bool()
  val ripRel   = Bool()
  val addr32   = Bool()
}

/** A fully decoded x86 instruction (one macro-op in this core). */
class Uop extends Bundle {
  val pc   = UInt(64.W)
  val len  = UInt(4.W)
  val op   = UInt(8.W)
  val simd = UInt(3.W)

  /** Operand A is the destination for two-operand instructions. */
  val a = new Operand
  /** Operand B is the source (register, memory or immediate). */
  val b = new Operand

  val imm = UInt(64.W)

  /** Effective operand width in bytes (1/2/4/8); 16 for packed SSE operations. */
  val opsz = UInt(5.W)
  /** Memory access width in bytes. */
  val memsz = UInt(5.W)

  val mem = new MemRef

  val cond = UInt(4.W)

  /** 0 = none, 1 = 0xF3 (REP/REPE), 2 = 0xF2 (REPNE) */
  val rep = UInt(2.W)

  val lock  = Bool()
  val valid = Bool()
  /** Raised by the decoder for illegal encodings; delivered at commit. */
  val illegal = Bool()
}

object Uop {
  def zero: Uop = {
    val u = Wire(new Uop)
    u := 0.U.asTypeOf(new Uop)
    u
  }
}
