// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tongxiaojun

package x86.elaborate

import chisel3.stage.ChiselStage
import x86.ooo.X86OooTop

/** Emit the Stage-B out-of-order core. */
object EmitOoo {
  def main(args: Array[String]): Unit = {
    val targetDir = if (args.nonEmpty) args(0) else "build/rtl"
    (new ChiselStage).emitVerilog(new X86OooTop, Array("--target-dir", targetDir))
  }
}
