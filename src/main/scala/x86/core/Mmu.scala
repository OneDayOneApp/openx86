package x86.core

import chisel3._
import chisel3.util._

/**
 * 4-level x86-64 page-table walker with a small direct-mapped TLB.
 *
 * The walker uses a dedicated page-table-walk port so that Stage-A does not
 * need to arbitrate with the load/store pipe. Translation supports 4 KiB
 * pages; large pages and user-mode permissions are reported as faults.
 */
class Mmu extends Module {
  val io = IO(new Bundle {
    val reqValid = Input(Bool())
    val vaddr    = Input(UInt(64.W))
    val isWrite  = Input(Bool())
    val isFetch  = Input(Bool())
    val pg       = Input(Bool())
    val wp       = Input(Bool())
    val nxe      = Input(Bool())
    val cr3      = Input(UInt(64.W))
    val flush    = Input(Bool())

    val busy   = Output(Bool())
    val done   = Output(Bool())
    val paddr  = Output(UInt(64.W))
    val fault  = Output(Bool())
    val isGp   = Output(Bool())
    val error  = Output(UInt(5.W))

    val ptwReq   = Output(Bool())
    val ptwAddr  = Output(UInt(64.W))
    val ptwData  = Input(UInt(64.W))
    val ptwValid = Input(Bool())
  })

  private val TlbEntries = 32
  private val tlbValid = RegInit(VecInit(Seq.fill(TlbEntries)(false.B)))
  private val tlbTag   = Reg(Vec(TlbEntries, UInt(40.W)))
  private val tlbPfn   = Reg(Vec(TlbEntries, UInt(40.W)))
  private val tlbW     = Reg(Vec(TlbEntries, Bool()))
  private val tlbNx    = Reg(Vec(TlbEntries, Bool()))

  private val vpn   = io.vaddr(51, 12)
  private val index = vpn(4, 0)
  private val offset = io.vaddr(11, 0)

  private val hit = tlbValid(index) && (tlbTag(index) === vpn)
  private val hitW = tlbW(index)
  private val hitNx = tlbNx(index)
  private val hitPfn = tlbPfn(index)

  private val hitPermFault = hit && (
    (io.isWrite && !hitW) ||
      (io.isFetch && io.nxe && hitNx))

  private val hitError = Cat(
    io.isFetch && io.nxe && hitNx, // bit 4: I/D
    0.U(1.W),                     // bit 3: RSVD
    0.U(1.W),                     // bit 2: U/S
    io.isWrite,                   // bit 1: W/R
    true.B                        // bit 0: P (protection)
  )

  // canonical check: bits 63:48 must equal bit 47
  private val canonical = (io.vaddr(63, 48) === Fill(16, io.vaddr(47))) || !io.pg

  private val state = RegInit(0.U(3.W)) // 0 idle, 1 walk, 2 done
  private val level = RegInit(0.U(2.W))
  private val walkAddr = Reg(UInt(64.W))
  private val reqVpn   = Reg(UInt(40.W))
  private val reqWrite = Reg(Bool())
  private val reqFetch = Reg(Bool())
  private val reqPaddr = Reg(UInt(64.W))
  private val reqFault = Reg(Bool())
  private val reqError = Reg(UInt(5.W))
  private val reqGp    = Reg(Bool())
  private val ptwWait  = RegInit(false.B)

  private def idx(l: UInt, v: UInt): UInt =
    Mux(l === 0.U, v(47, 39),
      Mux(l === 1.U, v(38, 30),
        Mux(l === 2.U, v(29, 21), v(20, 12))))

  io.busy := state =/= 0.U
  io.done := state === 2.U
  io.paddr := reqPaddr
  io.fault := state === 2.U && reqFault
  io.isGp  := state === 2.U && reqGp
  io.error := reqError

  io.ptwReq := state === 1.U && !ptwWait
  io.ptwAddr := walkAddr

  when(io.flush) {
    for (i <- 0 until TlbEntries) { tlbValid(i) := false.B }
  }

  switch(state) {
    is(0.U) {
      when(io.reqValid) {
        reqVpn   := vpn
        reqWrite := io.isWrite
        reqFetch := io.isFetch
        ptwWait  := false.B
        when(!io.pg) {
          reqPaddr := io.vaddr
          reqFault := false.B
          reqGp    := false.B
          reqError := 0.U
          state    := 2.U
        }.elsewhen(!canonical) {
          reqPaddr := 0.U
          reqFault := false.B
          reqGp    := true.B
          state    := 2.U
        }.elsewhen(hit && !hitPermFault) {
          reqPaddr := Cat(hitPfn, offset)
          reqFault := false.B
          reqGp    := false.B
          state    := 2.U
        }.elsewhen(hit && hitPermFault) {
          reqPaddr := 0.U
          reqFault := true.B
          reqGp    := false.B
          reqError := hitError
          state    := 2.U
        }.otherwise {
          level    := 0.U
          walkAddr := (io.cr3 & "hfffffffffffff000".U) + (idx(0.U, io.vaddr) << 3)
          state    := 1.U
        }
      }
    }
    is(1.U) {
      when(!ptwWait) { ptwWait := true.B }
      when(io.ptwValid) {
        val entry = io.ptwData
        val present = entry(0)
        val ps = entry(7)
        val writable = entry(1)
        val nx = entry(63)
        when(!present) {
          reqFault := true.B
          reqGp    := false.B
          reqError := Cat(io.isFetch, 0.U(1.W), 0.U(1.W), reqWrite, false.B)
          state := 2.U
        }.elsewhen(level =/= 3.U && ps) {
          // large pages unsupported
          reqFault := true.B
          reqGp    := false.B
          reqError := Cat(io.isFetch, true.B, 0.U(1.W), reqWrite, false.B)
          state := 2.U
        }.elsewhen(level === 3.U) {
          val pfn = entry(51, 12)
          val permFault = (reqWrite && !writable) || (reqFetch && io.nxe && nx)
          when(permFault) {
            reqFault := true.B
            reqGp    := false.B
            reqError := Cat(reqFetch && io.nxe && nx, 0.U(1.W), 0.U(1.W), reqWrite, true.B)
            state := 2.U
          }.otherwise {
            reqPaddr := Cat(pfn, offset)
            reqFault := false.B
            reqGp    := false.B
            reqError := 0.U
            tlbValid(index) := true.B
            tlbTag(index)   := reqVpn
            tlbPfn(index)   := pfn
            tlbW(index)     := writable
            tlbNx(index)    := nx
            state := 2.U
          }
        }.otherwise {
          val nextLevel = level + 1.U
          walkAddr := (entry(51, 12) << 12) + (idx(nextLevel, io.vaddr) << 3)
          level := nextLevel
          ptwWait := false.B
        }
      }
    }
    is(2.U) {
      when(!io.reqValid) { state := 0.U }
    }
  }
}
