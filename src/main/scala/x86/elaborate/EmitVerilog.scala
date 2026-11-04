package x86.elaborate

import chisel3.stage.ChiselStage
import x86.X86Top

/** Emit the top-level core as (System)Verilog. Usage: EmitVerilog [outDir] */
object EmitVerilog {
  def main(args: Array[String]): Unit = {
    val targetDir = if (args.nonEmpty) args(0) else "build/rtl"
    (new ChiselStage).emitVerilog(new X86Top, Array("--target-dir", targetDir))
  }
}
