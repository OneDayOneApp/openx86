package x86

import chisel3._

/** Toolchain smoke test: replaced by the real core once the flow is proven. */
class SmokeAdd extends Module {
  val io = IO(new Bundle {
    val a   = Input(UInt(64.W))
    val b   = Input(UInt(64.W))
    val sum = Output(UInt(64.W))
  })
  io.sum := io.a + io.b
}
