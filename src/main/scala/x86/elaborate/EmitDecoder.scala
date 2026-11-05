// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tongxiaojun

package x86.elaborate

import chisel3.stage.ChiselStage
import x86.frontend.Decoder

/** Emit the x86 decoder as Verilog for unit-level simulation. */
object EmitDecoder {
  def main(args: Array[String]): Unit = {
    val targetDir = if (args.nonEmpty) args(0) else "build/rtl"
    (new ChiselStage).emitVerilog(new Decoder, Array("--target-dir", targetDir))
  }
}
