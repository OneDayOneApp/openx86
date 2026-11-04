package x86

/** Global configuration for the XiangShan-inspired x86-64 core. */
object X86Config {
  // Architectural constants
  val XLEN = 64
  val NumGpr = 16
  val NumXmm = 16

  // Test-harness memory map (bare metal, ring 0)
  val ResetVector = 0x0000000000400000L
  val StackTop    = 0x0000000000800000L
  val HaltMmio    = 0x00000000f0000000L // write => halt, low 32 bits = exit code

  // Front-end
  val FetchBytes = 16 // fetch window per access (x86 max instruction length is 15)
  val MaxInstLen = 15
}

/** Exit codes returned through the halt MMIO port. */
object ExitCode {
  val Pass = 0
}
