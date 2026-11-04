package x86.core

import chisel3._
import chisel3.util._
import x86.isa._
import x86.X86Config
import x86.frontend.Decoder

/**
 * Stage-A in-order core.
 *
 * Stages: fetch+decode -> execute -> memory -> writeback. Register operands are
 * read in the execute stage so values produced by instructions that have left
 * the pipeline are still visible through the register file. Memory operations
 * hold the front-end until the access completes; branches resolve in EX and RET
 * resolves at the memory response.
 */
class X86Core extends Module {
  val io = IO(new Bundle {
    val imem = new Bundle {
      val req  = Output(new IMemReq)
      val resp = Input(new IMemResp)
    }
    val dmem = new Bundle {
      val req  = Output(new DMemReq)
      val resp = Input(new DMemResp)
    }
    val ptw = new Bundle {
      val req  = Output(new PtwReq)
      val resp = Input(new PtwResp)
    }
    val halted    = Output(Bool())
    val exitCode  = Output(UInt(64.W))
    val fatal     = Output(Bool())
    val fatalPc   = Output(UInt(64.W))
    val fatalCode = Output(UInt(8.W))
    val commit    = Output(new CommitTrace)
    val dbg = Output(new Bundle {
      val gpr    = Vec(16, UInt(64.W))
      val rip    = UInt(64.W)
      val flags  = UInt(64.W)
      val cycles = UInt(64.W)
      val xmm    = Vec(16, UInt(128.W))
      val exValid = Bool()
      val exPc = UInt(64.W)
      val exOp = UInt(8.W)
      val exAIdx = UInt(5.W)
      val exA = UInt(64.W)
      val exB = UInt(64.W)
      val aluA = UInt(64.W)
      val aluB = UInt(64.W)
    })
  })

  private val HaltMmio = X86Config.HaltMmio.U(64.W)

  private def lut[T <: Data](k: UInt, d: T, m: Seq[(Any, T)]): T =
    m.foldRight(d) { case ((key, v), acc) =>
      val kk: UInt = key match {
        case i: Int  => i.U
        case u: UInt => u
        case other   => throw new IllegalArgumentException(s"bad lut key: $other")
      }
      Mux(k === kk, v, acc)
    }

  val gpr = Module(new GprFile)
  val xmm = Module(new XmmFile)
  val pc  = RegInit(X86Config.ResetVector.U(64.W))
  val flagsReg  = RegInit("h202".U(64.W))
  val cr0 = RegInit(0.U(64.W))
  val cr2 = RegInit(0.U(64.W))
  val cr3 = RegInit(0.U(64.W))
  val cr4 = RegInit(0.U(64.W))
  val efer = RegInit(0.U(64.W))
  val star = RegInit(0.U(64.W))
  val lstar = RegInit(0.U(64.W))
  val sfmask = RegInit(0.U(64.W))
  val idtrBase = RegInit(0.U(64.W))
  val idtrLimit = RegInit(0xFFFF.U(64.W))

  val sysActive = RegInit(false.B)
  val sysOp   = RegInit(0.U(2.W)) // 0 = trap entry, 1 = lidt
  val sysStep = RegInit(0.U(4.W))
  val sysWait = RegInit(false.B)
  val sysData = Reg(UInt(64.W))
  val sysWe   = Reg(Bool())
  val trapVecR = Reg(UInt(8.W))
  val trapErrR = Reg(UInt(32.W))
  val trapPcR  = Reg(UInt(64.W))
  val trapRspR = Reg(UInt(64.W))
  val trapHandlerR = Reg(UInt(64.W))
  val idtLowR = Reg(UInt(64.W))
  val lidtAddrR = Reg(UInt(64.W))
  val lidtLowR = Reg(UInt(64.W))
  val iretFlags = Reg(UInt(64.W))
  val iretRsp = Reg(UInt(64.W))
  val cpuidEax = Reg(UInt(32.W))
  val cpuidEbx = Reg(UInt(32.W))
  val cpuidEcx = Reg(UInt(32.W))
  val cpuidEdx = Reg(UInt(32.W))
  val haltedReg = RegInit(false.B)
  val exitReg   = RegInit(0.U(64.W))
  val fatalReg  = RegInit(false.B)
  val fatalPcReg = RegInit(0.U(64.W))
  val fatalCdReg = RegInit(0.U(8.W))
  val cycleCnt  = RegInit(0.U(64.W))
  cycleCnt := cycleCnt + 1.U

  // ------------------------------------------------------------------- fetch
  val ibuf      = Reg(UInt(256.W))
  val ibufBase  = Reg(UInt(64.W))
  val ibufValid = RegInit(false.B)
  val stopFetch = RegInit(false.B)

  // Fetch line acquisition FSM. With paging enabled the 16B-aligned line base
  // is translated through the shared MMU before the 32B imem request is sent:
  //   0 = idle/need line, 1 = MMU translation, 2 = imem request, 3 = imem wait.
  val fState     = RegInit(0.U(2.W))
  val fetchVaddr = Reg(UInt(64.W))
  val fetchPaddr = Reg(UInt(64.W))

  private val lineBase = Cat(pc(63, 4), 0.U(4.W))
  private val canUse   = ibufValid && (pc >= ibufBase) && (pc < (ibufBase + 16.U))
  private val needLine = !canUse && !haltedReg && !stopFetch && !sysActive

  private val off    = pc - ibufBase
  private val window = (ibuf >> (off << 3))(127, 0)
  private val decBytes = Wire(Vec(16, UInt(8.W)))
  for (i <- 0 until 16) { decBytes(i) := window(8 * i + 7, 8 * i) }

  val dec = Module(new Decoder)
  dec.io.pc    := pc
  dec.io.bytes := decBytes
  private val d = dec.io.uop

  private def isSse(u: Uop): Bool = u.op >= Ops.MOVD.U && u.op <= Ops.CVTTPD2DQ.U

  // Instructions this stage cannot execute yet.
  private val aluWithMemDst = false.B

  private val aluMemSrc = d.b.kind === OperandKind.Mem.U && (
      d.op === Ops.ADD.U || d.op === Ops.ADC.U || d.op === Ops.SUB.U || d.op === Ops.SBB.U ||
      d.op === Ops.AND.U || d.op === Ops.OR.U || d.op === Ops.XOR.U ||
      d.op === Ops.CMP.U || d.op === Ops.TEST.U)

  private val sseMemArith = d.b.kind === OperandKind.Mem.U && (
    d.op === Ops.CVTSI2SD.U || d.op === Ops.CVTSI2SS.U || d.op === Ops.CVTSS2SD.U ||
      d.op === Ops.CVTSD2SS.U || d.op === Ops.CVTDQ2PS.U || d.op === Ops.CVTPS2DQ.U ||
      d.op === Ops.CVTDQ2PD.U || d.op === Ops.CVTPD2DQ.U || d.op === Ops.CVTTPD2DQ.U)

  private val aluMemStore = d.a.kind === OperandKind.Mem.U && (
    d.op === Ops.ADD.U || d.op === Ops.SUB.U || d.op === Ops.AND.U ||
    d.op === Ops.OR.U || d.op === Ops.XOR.U || d.op === Ops.ADC.U ||
    d.op === Ops.SBB.U)
  private val unsupported = sseMemArith || d.op === Ops.MOVS.U || d.op === Ops.STOS.U || d.op === Ops.LODS.U ||
    d.op === Ops.SCAS.U || d.op === Ops.CMPS.U ||
    d.op === Ops.INT3.U ||
    aluWithMemDst ||
    (d.op === Ops.CALL.U && (d.a.kind === OperandKind.Mem.U || d.b.kind === OperandKind.Mem.U))

  // -------------------------------------------------------------- pipeline regs
  class IdEx extends Bundle {
    val valid = Bool()
    val uop   = new Uop
    val bad   = Bool()
  }

  class ExMem extends Bundle {
    val valid     = Bool()
    val uop       = new Uop
    val result    = UInt(64.W)
    val hi        = UInt(64.W)
    val flags     = UInt(64.W)
    val wf        = Bool()
    val addr      = UInt(64.W)
    val storeData = UInt(128.W)
    val target    = UInt(64.W)
    val taken     = Bool()
    val isLoad    = Bool()
    val isStore   = Bool()
    val writeHi   = Bool()
    val divErr    = Bool()
    val rsp       = UInt(64.W)
    val rspNext   = UInt(64.W)
    val oldDst    = UInt(64.W)
    val aVal      = UInt(64.W)
    val bVal      = UInt(64.W)
    val xmmResult = UInt(128.W)
    val xmmA      = UInt(128.W)
    val bad       = Bool()
  }

  class MemWb extends Bundle {
    val valid   = Bool()
    val uop     = new Uop
    val result  = UInt(64.W)
    val hi      = UInt(64.W)
    val flags   = UInt(64.W)
    val wf      = Bool()
    val writeHi = Bool()
    val rspNext = UInt(64.W)
    val oldDst  = UInt(64.W)
    val xmmResult = UInt(128.W)
    val bad     = Bool()
  }

  val idEx    = RegInit(0.U.asTypeOf(new IdEx))
  val exMem   = RegInit(0.U.asTypeOf(new ExMem))
  val memWb   = RegInit(0.U.asTypeOf(new MemWb))
  val memWait = RegInit(false.B)

  // ------------------------------------------------------------------ EX read
  gpr.io.rs1 := idEx.uop.a.idx
  gpr.io.rs2 := idEx.uop.b.idx
  gpr.io.rs3 := idEx.uop.mem.base
  gpr.io.rs4 := idEx.uop.mem.index
  gpr.io.rs5 := 0.U
  gpr.io.rs6 := 2.U
  gpr.io.rs7 := 4.U
  xmm.io.rs1 := idEx.uop.a.idx
  xmm.io.rs2 := idEx.uop.b.idx

  private def fwdX(idx: UInt, v: UInt): UInt = {
    val fromEx = exMem.valid && (exMem.uop.a.kind === OperandKind.Xmm.U) && (exMem.uop.a.idx === idx)
    val fromWb = memWb.valid && (memWb.uop.a.kind === OperandKind.Xmm.U) && (memWb.uop.a.idx === idx)
    Mux(fromEx, exMem.xmmResult, Mux(fromWb, memWb.xmmResult, v))
  }

  private def isStack(u: Uop): Bool =
    u.op === Ops.PUSH.U || u.op === Ops.POP.U || u.op === Ops.CALL.U ||
      u.op === Ops.RET.U || u.op === Ops.PUSHF.U || u.op === Ops.POPF.U

  private def writesReg(u: Uop): Bool =
    u.a.kind === OperandKind.Gpr.U &&
      u.op =/= Ops.CMP.U && u.op =/= Ops.TEST.U && u.op =/= Ops.PUSH.U &&
      u.op =/= Ops.JMP.U && u.op =/= Ops.JCC.U && u.op =/= Ops.CALL.U &&
      u.op =/= Ops.RET.U && u.op =/= Ops.HLT.U && u.op =/= Ops.NOP.U &&
      u.op =/= Ops.CDQ.U && u.op =/= Ops.PUSHF.U && u.op =/= Ops.POPF.U &&
      u.op =/= Ops.CLC.U && u.op =/= Ops.STC.U && u.op =/= Ops.CMC.U &&
      u.op =/= Ops.CLD.U && u.op =/= Ops.STD.U && u.op =/= Ops.CLI.U && u.op =/= Ops.STI.U &&
      u.op =/= Ops.SAHF.U &&
      u.op =/= Ops.MUL.U && u.op =/= Ops.DIV.U && u.op =/= Ops.IDIV.U &&
      !(u.op === Ops.IMUL.U && u.b.kind === OperandKind.None.U) ||
      (u.op === Ops.XCHG.U && u.b.kind === OperandKind.Gpr.U)

  private def dstRegIdx(u: Uop): UInt =
    Mux((u.op === Ops.XCHG.U || u.op === Ops.XADD.U) &&
      u.a.kind === OperandKind.Mem.U, u.b.idx, u.a.idx)

  private def fwd(idx: UInt, v: UInt): UInt = {
    val exRsp = exMem.valid && isStack(exMem.uop) && (idx === 4.U)
    val wbRsp = memWb.valid && isStack(memWb.uop) && (idx === 4.U)
    val fromEx = exMem.valid && !exMem.isLoad && writesReg(exMem.uop) && (dstRegIdx(exMem.uop) === idx)
    val fromWb = memWb.valid && writesReg(memWb.uop) && (dstRegIdx(memWb.uop) === idx)
    val exRax = exMem.valid && exMem.writeHi && (idx === 0.U) && exMem.uop.op =/= Ops.CDQ.U
    val exRdx = exMem.valid && exMem.writeHi && (idx === 2.U)
    val wbRax = memWb.valid && memWb.writeHi && (idx === 0.U) && memWb.uop.op =/= Ops.CDQ.U
    val wbRdx = memWb.valid && memWb.writeHi && (idx === 2.U)
    Mux(exRsp, exMem.rspNext,
      Mux(wbRsp, memWb.rspNext,
        Mux(exRax, exMem.result,
          Mux(exRdx, exMem.hi,
            Mux(wbRax, memWb.result,
              Mux(wbRdx, memWb.hi,
                Mux(fromEx, mergeValue(exMem.oldDst, exMem.result, exMem.uop.opsz),
      Mux(fromWb, mergeValue(memWb.oldDst, memWb.result, memWb.uop.opsz), v))))))))
  }

  private val flagsEx = Mux(
    exMem.valid && exMem.wf,
    exMem.flags,
    Mux(memWb.valid && memWb.wf, memWb.flags, flagsReg)
  )

  private val exA     = fwd(idEx.uop.a.idx, gpr.io.rs1Data)
  private val exB     = fwd(idEx.uop.b.idx, gpr.io.rs2Data)
  private val exBase  = fwd(idEx.uop.mem.base, gpr.io.rs3Data)
  private val exIndex = fwd(idEx.uop.mem.index, gpr.io.rs4Data)
  private val exRax   = fwd(0.U, gpr.io.rs5Data)
  private val exRdx   = fwd(2.U, gpr.io.rs6Data)
  private val exRsp   = fwd(4.U, gpr.io.rs7Data)

  private val ea = WireDefault(0.U(64.W))
  when(idEx.uop.mem.ripRel) {
    ea := idEx.uop.pc + idEx.uop.len + idEx.uop.mem.disp
  }.otherwise {
    ea := Mux(idEx.uop.mem.hasBase, exBase, 0.U) +
      Mux(idEx.uop.mem.hasIndex, exIndex << idEx.uop.mem.scale, 0.U) +
      idEx.uop.mem.disp
  }

  private val alu = Module(new IntAlu)

  private val srcBits = idEx.uop.memsz << 3
  private val movExt = lut(idEx.uop.op, exB,
    Seq(
      Ops.MOVZX -> {
        val m = ((1.U(64.W) << srcBits) - 1.U)(63, 0)
        exB & m
      },
      Ops.MOVSX -> {
        val m = ((1.U(64.W) << srcBits) - 1.U)(63, 0)
        val sign = (exB >> (srcBits - 1.U))(0)
        (exB & m) | Mux(sign, ~m, 0.U(64.W))
      },
      Ops.MOVSXD -> {
        val sign = exB(31)
        (exB & "hffffffff".U(64.W)) | Mux(sign, "hffffffff00000000".U(64.W), 0.U(64.W))
      })
    )

  private val isOneOp = (idEx.uop.op === Ops.MUL.U || idEx.uop.op === Ops.IMUL.U ||
    idEx.uop.op === Ops.DIV.U || idEx.uop.op === Ops.IDIV.U) &&
    idEx.uop.b.kind === OperandKind.None.U

  private val isImul3 = idEx.uop.op === Ops.IMUL.U && idEx.uop.imm =/= 0.U && !isOneOp

  private val aluA = lut(idEx.uop.op, exA,
    Seq(
      Ops.MUL  -> Mux(isOneOp, exRax, exA),
      Ops.IMUL -> Mux(isOneOp, exRax, Mux(isImul3, exB, exA)),
      Ops.DIV  -> Mux(isOneOp, exRax, exA),
      Ops.IDIV -> Mux(isOneOp, exRax, exA),
      Ops.CDQ  -> exRax,
      Ops.CBW  -> exA)
    )

  private val bSrc = Mux(idEx.uop.b.kind === OperandKind.Imm.U, idEx.uop.imm, exB)

  private val aluB = lut(idEx.uop.op, bSrc,
    Seq(
      Ops.MUL  -> Mux(isOneOp, exA, bSrc),
      Ops.IMUL -> Mux(isOneOp, exA, Mux(isImul3, idEx.uop.imm, bSrc)),
      Ops.DIV  -> Mux(isOneOp, exA, bSrc),
      Ops.IDIV -> Mux(isOneOp, exA, bSrc),
      Ops.INC   -> 1.U,
      Ops.DEC   -> 1.U,
      Ops.NEG   -> 0.U,
      Ops.LEA   -> 0.U,
      Ops.MOVZX -> movExt,
      Ops.MOVSX -> movExt,
      Ops.MOVSXD -> movExt)
    )

  alu.io.op      := idEx.uop.op
  alu.io.opsz    := idEx.uop.opsz
  alu.io.a       := aluA
  alu.io.b       := aluB
  alu.io.hiIn    := exRdx
  alu.io.imm     := idEx.uop.imm
  alu.io.addrIn  := ea
  alu.io.flagsIn := flagsEx
  alu.io.cond    := idEx.uop.cond
  alu.io.oneOp   := isOneOp

  // ------------------------------------------------------------------ SSE
  val sse = Module(new SseAlu)
  private val exXa = fwdX(idEx.uop.a.idx, xmm.io.rs1Data)
  private val exXb = fwdX(idEx.uop.b.idx, xmm.io.rs2Data)

  private val gprAsXmm = Mux(idEx.uop.opsz === 4.U,
    Cat(0.U(96.W), exB(31, 0)), Cat(0.U(64.W), exB))
  private val sseB = Mux(idEx.uop.b.kind === OperandKind.Xmm.U, exXb,
    Mux(idEx.uop.b.kind === OperandKind.Gpr.U, gprAsXmm, 0.U(128.W)))

  sse.io.op   := idEx.uop.op
  sse.io.simd := idEx.uop.simd
  sse.io.opsz := idEx.uop.opsz
  sse.io.a    := Mux(idEx.uop.a.kind === OperandKind.Xmm.U, exXa, 0.U(128.W))
  sse.io.b    := sseB
  sse.io.imm  := idEx.uop.imm(7, 0)
  sse.io.flagsIn := flagsEx

  private val simdMerge = Mux(idEx.uop.simd === SimdKind.SS.U,
    Cat(exXa(127, 32), sse.io.result(31, 0)),
    Mux(idEx.uop.simd === SimdKind.SD.U,
      Cat(exXa(127, 64), sse.io.result(63, 0)), sse.io.result))

  private val sseMerge = lut(idEx.uop.op, simdMerge,
    Seq(
      Ops.MOVD -> Mux(idEx.uop.opsz === 8.U,
        Cat(0.U(64.W), sse.io.result(63, 0)), Cat(0.U(96.W), sse.io.result(31, 0))),
      Ops.MOVQ -> Cat(0.U(64.W), sse.io.result(63, 0)),
      Ops.CVTSS2SD -> Cat(exXa(127, 64), sse.io.result(63, 0)),
      Ops.CVTSD2SS -> Cat(exXa(127, 32), sse.io.result(31, 0)),
      Ops.CVTSI2SD -> Cat(exXa(127, 64), sse.io.result(63, 0)),
      Ops.CVTSI2SS -> Cat(exXa(127, 32), sse.io.result(31, 0))
    ))

  private val movdToGpr = Mux(idEx.uop.memsz === 8.U, exXb(63, 0), exXb(31, 0).pad(64))

  private val crIdx = idEx.uop.imm(3, 0)
  private val crVal = lut(crIdx, 0.U(64.W),
    Seq(0.U -> cr0, 2.U -> cr2, 3.U -> cr3, 4.U -> cr4, 8.U -> 0.U(64.W)))
  private val isCrWrite = idEx.uop.op === Ops.MOV_CR.U && idEx.uop.cond(0)
  private val isWrmsr = idEx.uop.op === Ops.WRMSR.U
  private val isSyscall = idEx.uop.op === Ops.SYSCALL.U
  private val isSysret = idEx.uop.op === Ops.SYSRET.U
  private val isSerial = isCrWrite || isWrmsr || isSyscall || isSysret

  private val msrSel = exA(31, 0)
  private val msrVal = lut(msrSel, 0.U(64.W),
    Seq("hc0000080".U -> efer, "hc0000081".U -> star,
      "hc0000082".U -> lstar, "hc0000084".U -> sfmask))
  private val msrWriteVal = Cat(gpr.io.rs6Data(31, 0), exB(31, 0))

  val mmu = Module(new Mmu)
  // Shared-MMU arbitration: 0 = free, 1 = data translation, 2 = fetch translation.
  val mmuOwner = RegInit(0.U(2.W))
  mmu.io.pg      := cr0(31)
  mmu.io.wp      := cr0(16)
  mmu.io.nxe     := efer(11)
  mmu.io.cr3     := cr3
  mmu.io.flush   := (idEx.valid && isCrWrite && crIdx === 3.U) ||
    (idEx.valid && idEx.uop.op === Ops.INVLPG.U)
  mmu.io.ptwData := io.ptw.resp.data
  mmu.io.ptwValid := io.ptw.resp.valid
  io.ptw.req.valid := mmu.io.ptwReq
  io.ptw.req.addr  := mmu.io.ptwAddr

  when(idEx.valid && isCrWrite) {
    when(crIdx === 0.U) { cr0 := exA }
    when(crIdx === 2.U) { cr2 := exA }
    when(crIdx === 3.U) { cr3 := exA }
    when(crIdx === 4.U) { cr4 := exA }
  }

  // ------------------------------------------------------------------ branch
  private val relTarget = idEx.uop.pc + idEx.uop.len + idEx.uop.imm
  private val taken  = WireDefault(false.B)
  private val target = WireDefault(relTarget)

  when(idEx.valid && isCrWrite) {
    taken  := true.B
    target := idEx.uop.pc + idEx.uop.len
  }
  when(idEx.valid && (isWrmsr || isSyscall || isSysret || idEx.uop.op === Ops.INVLPG.U)) {
    taken  := true.B
    target := Mux(isSyscall, lstar,
      Mux(isSysret, exA, idEx.uop.pc + idEx.uop.len))
  }

  when(idEx.valid && isWrmsr) {
    when(msrSel === "hc0000080".U) { efer := msrWriteVal }
    when(msrSel === "hc0000081".U) { star := msrWriteVal }
    when(msrSel === "hc0000082".U) { lstar := msrWriteVal }
    when(msrSel === "hc0000084".U) { sfmask := msrWriteVal }
  }
  when(idEx.valid && isSyscall) {
    flagsReg := flagsReg & ~sfmask
  }
  when(idEx.valid && isSysret) {
    flagsReg := exB
  }

  when(idEx.valid) {
    when(idEx.uop.op === Ops.JCC.U) {
      taken  := alu.io.branchTaken
      target := relTarget
    }.elsewhen(idEx.uop.op === Ops.JMP.U) {
      when(idEx.uop.a.kind === OperandKind.Mem.U) {
        taken := false.B // target comes from the memory load, resolved at WB
      }.otherwise {
        taken := true.B
        when(idEx.uop.b.kind === OperandKind.Imm.U) { target := relTarget }
          .otherwise { target := exA }
      }
    }.elsewhen(idEx.uop.op === Ops.LOOP.U) {
      taken := alu.io.result =/= 0.U
      target := relTarget
    }.elsewhen(idEx.uop.op === Ops.LOOPNE.U) {
      taken := (alu.io.result =/= 0.U) && !flagsReg(FlagBit.ZF)
      target := relTarget
    }.elsewhen(idEx.uop.op === Ops.LOOPE.U) {
      taken := (alu.io.result =/= 0.U) && flagsReg(FlagBit.ZF)
      target := relTarget
    }.elsewhen(idEx.uop.op === Ops.JRCXZ.U) {
      taken := exA === 0.U
      target := relTarget
    }.elsewhen(idEx.uop.op === Ops.CALL.U) {
      taken := true.B
      when(idEx.uop.b.kind === OperandKind.Imm.U) { target := relTarget }
        .otherwise { target := exA }
    }
  }

  private val specialFlags = WireDefault(alu.io.flags)
  private val isSpecial = WireDefault(false.B)

  when(idEx.valid) {
    when(idEx.uop.op === Ops.CLC.U) { specialFlags := flagsEx & ~(1.U << FlagBit.CF); isSpecial := true.B }
    when(idEx.uop.op === Ops.STC.U) { specialFlags := flagsEx | (1.U << FlagBit.CF); isSpecial := true.B }
    when(idEx.uop.op === Ops.CMC.U) { specialFlags := flagsEx ^ (1.U << FlagBit.CF); isSpecial := true.B }
    when(idEx.uop.op === Ops.CLD.U) { specialFlags := flagsEx & ~(1.U << FlagBit.DF); isSpecial := true.B }
    when(idEx.uop.op === Ops.STD.U) { specialFlags := flagsEx | (1.U << FlagBit.DF); isSpecial := true.B }
    when(idEx.uop.op === Ops.CLI.U) { specialFlags := flagsEx & ~(1.U << FlagBit.IF); isSpecial := true.B }
    when(idEx.uop.op === Ops.STI.U) { specialFlags := flagsEx | (1.U << FlagBit.IF); isSpecial := true.B }
    when(idEx.uop.op === Ops.SAHF.U) {
      val m = ((BigInt(1) << FlagBit.CF) | (BigInt(1) << FlagBit.PF) |
        (BigInt(1) << FlagBit.AF) | (BigInt(1) << FlagBit.ZF) |
        (BigInt(1) << FlagBit.SF)).U(64.W)
      val b = Cat(exA(15), exA(14), 0.U(1.W), exA(12), 0.U(1.W), exA(10), 1.U(1.W), exA(8)).pad(64)
      specialFlags := (flagsEx & ~m) | (b & m)
      isSpecial := true.B
    }
  }

  // --------------------------------------------------------------------- MEM
  private def hasMem(u: Uop): Bool =
    u.op =/= Ops.LEA.U && (u.a.kind === OperandKind.Mem.U || u.b.kind === OperandKind.Mem.U ||
      u.op === Ops.PUSH.U || u.op === Ops.POP.U || u.op === Ops.CALL.U || u.op === Ops.RET.U ||
      u.op === Ops.PUSHF.U || u.op === Ops.POPF.U)

  private val exHasMem = exMem.valid && hasMem(exMem.uop)

  private def memLoadOf(u: Uop): Bool = u.op =/= Ops.LEA.U && (
    u.b.kind === OperandKind.Mem.U ||
      u.op === Ops.POP.U || u.op === Ops.RET.U || u.op === Ops.POPF.U ||
      (u.op === Ops.JMP.U && u.a.kind === OperandKind.Mem.U) ||
      (u.op === Ops.CALL.U && u.a.kind === OperandKind.Mem.U))

  private def memStoreOf(u: Uop): Bool =
    (u.a.kind === OperandKind.Mem.U &&
      u.op =/= Ops.CMP.U && u.op =/= Ops.TEST.U && u.op =/= Ops.LEA.U &&
      u.op =/= Ops.JMP.U && u.op =/= Ops.CALL.U) ||
      (u.op === Ops.PUSH.U) ||
      (u.op === Ops.CALL.U && u.a.kind =/= OperandKind.Mem.U) ||
      u.op === Ops.PUSHF.U

  private val isLoad = memLoadOf(exMem.uop)
  private val isStore = memStoreOf(exMem.uop)

  private val memAddr = exMem.addr
  private val isRmw = exMem.valid && exMem.uop.a.kind === OperandKind.Mem.U &&
    (exMem.uop.op === Ops.ADD.U || exMem.uop.op === Ops.SUB.U ||
      exMem.uop.op === Ops.AND.U || exMem.uop.op === Ops.OR.U ||
      exMem.uop.op === Ops.XOR.U || exMem.uop.op === Ops.ADC.U ||
      exMem.uop.op === Ops.SBB.U || exMem.uop.op === Ops.INC.U ||
      exMem.uop.op === Ops.DEC.U || exMem.uop.op === Ops.NEG.U ||
      exMem.uop.op === Ops.NOT.U || exMem.uop.op === Ops.SHL.U ||
      exMem.uop.op === Ops.SHR.U || exMem.uop.op === Ops.SAR.U ||
      exMem.uop.op === Ops.ROL.U || exMem.uop.op === Ops.ROR.U ||
      exMem.uop.op === Ops.RCL.U || exMem.uop.op === Ops.RCR.U ||
      exMem.uop.op === Ops.XCHG.U || exMem.uop.op === Ops.XADD.U)
  val rmwPhase = RegInit(false.B)
  val rmwResult = RegInit(0.U(64.W))
  val rmwFlags = RegInit(0.U(64.W))
  val rmwLoaded = RegInit(0.U(64.W))
  private val memSize = Mux(
    (exMem.uop.op === Ops.JMP.U || exMem.uop.op === Ops.CALL.U) &&
      exMem.uop.a.kind === OperandKind.Mem.U, 8.U,
    Mux((exMem.uop.op === Ops.XCHG.U || exMem.uop.op === Ops.XADD.U) &&
      exMem.uop.a.kind === OperandKind.Mem.U,
      exMem.uop.opsz, exMem.uop.memsz))
  private val storeValue = exMem.storeData
  private val storeMask = ((1.U(17.W) << memSize) - 1.U)(15, 0)

  private val isHaltStore = isStore && (memAddr === HaltMmio)

  private val trapHasErr = trapVecR === 14.U || trapVecR === 13.U ||
    trapVecR === 8.U || trapVecR === 17.U

  private val trapReq = WireDefault(false.B)
  private val trapReqVec = WireDefault(0.U(8.W))
  private val trapReqErr = WireDefault(0.U(32.W))
  private val trapReqPc = WireDefault(0.U(64.W))

  when(idEx.valid && idEx.uop.op === Ops.CPUID.U) {
    val leaf = exA(31, 0)
    sysActive := true.B
    sysOp := 3.U
    sysStep := 0.U
    sysWait := false.B
    cpuidEax := Mux(leaf === 0.U, 1.U,
      Mux(leaf === 1.U, "h00000663".U,
        Mux(leaf === "h80000000".U, "h80000001".U, 0.U)))
    cpuidEbx := Mux(leaf === 0.U, "h2d363858".U, 0.U)
    cpuidEcx := Mux(leaf === 0.U, "h65726f43".U,
      Mux(leaf === 1.U, "h07008020".U,
        Mux(leaf === "h80000001".U, 1.U, 0.U)))
    cpuidEdx := Mux(leaf === 0.U, "h2d484d4b".U,
      Mux(leaf === 1.U, "h07008970".U,
        Mux(leaf === "h80000001".U, "h20100800".U, 0.U)))
    idEx.valid := false.B
    ibufValid := false.B
  }

  when(idEx.valid && idEx.uop.op === Ops.IRET.U) {
    sysActive := true.B
    sysOp := 2.U
    sysStep := 0.U
    sysWait := false.B
    trapRspR := gpr.io.rs7Data
    idEx.valid := false.B
    ibufValid := false.B
  }

  when(idEx.valid && idEx.uop.op === Ops.LIDT.U) {
    sysActive := true.B
    sysOp := 1.U
    sysStep := 0.U
    sysWait := false.B
    lidtAddrR := ea
    idEx.valid := false.B
    ibufValid := false.B
  }

  val xlatPending = RegInit(false.B)
  val xlatReady   = RegInit(false.B)
  val xlatPaddr   = Reg(UInt(64.W))

  private val needXlat = exHasMem && !memWait && !xlatReady && !isHaltStore && !haltedReg
  when(needXlat) { xlatPending := true.B }
  when(mmu.io.done && mmuOwner === 1.U && !xlatReady) {
    xlatPending := false.B
    when(mmu.io.fault || mmu.io.isGp) {
      trapReq := true.B
      trapReqVec := Mux(mmu.io.isGp, 13.U, 14.U)
      trapReqErr := mmu.io.error.pad(32)
      trapReqPc := exMem.uop.pc
    }.otherwise {
      xlatPaddr := mmu.io.paddr
      xlatReady := true.B
    }
  }

  private val sysBase = idtrBase + (trapVecR << 4)
  private val sysFinalTrap = sysOp === 0.U && sysStep === 8.U
  private val sysFinalLidt = sysOp === 1.U && sysStep === 2.U
  private val sysFinalIret = sysOp === 2.U && sysStep === 5.U
  private val sysNoErrSkip = sysOp === 0.U && sysStep === 7.U && !trapHasErr
  private val sysNeedMem = sysActive && !sysWait && sysOp =/= 3.U &&
    !sysFinalTrap && !sysFinalLidt && !sysFinalIret && !sysNoErrSkip
  when(sysActive && sysOp === 3.U && !sysWait) { sysStep := sysStep + 1.U }

  private val sysAddr = Mux(sysOp === 1.U,
    lidtAddrR + Mux(sysStep === 0.U, 0.U, 8.U),
    Mux(sysOp === 2.U, trapRspR + (8.U * sysStep),
      Mux(sysStep === 0.U, sysBase,
        Mux(sysStep === 1.U, sysBase + 8.U,
          trapRspR - (8.U * (sysStep - 1.U))))))
  private val sysWriteData = lut(sysStep, 0.U(64.W),
    Seq(2.U -> 0.U(64.W), 3.U -> trapRspR, 4.U -> flagsReg,
      5.U -> 8.U(64.W), 6.U -> trapPcR, 7.U -> trapErrR.pad(64)))

  private val normMem = exHasMem && xlatReady && !memWait && !isHaltStore && !sysActive

  io.dmem.req.valid := Mux(sysActive, sysNeedMem, normMem)
  io.dmem.req.we    := Mux(sysActive, sysOp === 0.U && sysStep >= 2.U,
    Mux(isRmw && !rmwPhase, false.B, isStore))
  io.dmem.req.addr  := Mux(sysActive, sysAddr, Mux(xlatReady, xlatPaddr, memAddr))
  io.dmem.req.wdata := Mux(sysActive, sysWriteData,
    Mux(isRmw && rmwPhase, rmwResult, storeValue))
  io.dmem.req.wmask := Mux(sysActive, "hff".U(16.W), storeMask)
  io.dmem.req.size  := Mux(sysActive, 8.U, memSize)

  when(sysNeedMem) { sysWait := true.B }
  when(sysActive && sysOp === 0.U && sysStep === 7.U && !trapHasErr) {
    sysStep := 8.U
  }

  when(sysActive && sysWait && io.dmem.resp.valid) {
    sysWait := false.B
    when(sysOp === 0.U) {
      when(sysStep === 0.U) {
        idtLowR := io.dmem.resp.rdata(63, 0)
        sysStep := 1.U
      }.elsewhen(sysStep === 1.U) {
        trapHandlerR := Cat(io.dmem.resp.rdata(31, 0), idtLowR(63, 48), idtLowR(15, 0))
        sysStep := 2.U
      }.elsewhen(sysStep < 8.U) {
        sysStep := sysStep + 1.U
      }
    }.elsewhen(sysOp === 1.U) {
      when(sysStep === 0.U) {
        lidtLowR := io.dmem.resp.rdata(63, 0)
        sysStep := 1.U
      }.otherwise {
        idtrBase := (lidtLowR >> 16) | (io.dmem.resp.rdata(15, 0) << 48)
        idtrLimit := lidtLowR(15, 0)
        sysStep := 2.U
      }
    }.otherwise {
      when(sysStep === 0.U) {
        trapHandlerR := io.dmem.resp.rdata(63, 0)
        sysStep := 1.U
      }.elsewhen(sysStep === 2.U) {
        iretFlags := io.dmem.resp.rdata(63, 0)
        sysStep := 3.U
      }.elsewhen(sysStep === 3.U) {
        iretRsp := io.dmem.resp.rdata(63, 0)
        sysStep := 4.U
      }.otherwise {
        sysStep := sysStep + 1.U
      }
    }
  }

  when(exHasMem && xlatReady && !memWait && !isHaltStore) {
    memWait := true.B
    xlatReady := false.B
  }
  when(exHasMem && isHaltStore && !memWait) {
    haltedReg := true.B
    exitReg   := storeValue(63, 0)
    pc        := exMem.uop.pc + exMem.uop.len
    exMem.valid := false.B
  }

  private val memResp  = memWait && io.dmem.resp.valid
  private val loadData = io.dmem.resp.rdata(63, 0)

  private def extendLoad(x: UInt, u: Uop): UInt = {
    val bits = u.memsz << 3
    val m = ((1.U(64.W) << bits) - 1.U)(63, 0)
    val sign = (x >> (bits - 1.U))(0)
    val sext = (x & m) | Mux(sign, ~m, 0.U(64.W))
    Mux(u.op === Ops.MOVZX.U, x & m,
      Mux(u.op === Ops.MOVSX.U, sext,
        Mux(u.op === Ops.MOVSXD.U,
          (x & "hffffffff".U(64.W)) | Mux(x(31), "hffffffff00000000".U(64.W), 0.U(64.W)), x)))
  }
  private val loadValue = extendLoad(loadData, exMem.uop)

  private def sseLoadValue(x: UInt, u: Uop): UInt =
    Mux(u.op === Ops.MOVD.U,
      Mux(u.memsz === 8.U, Cat(0.U(64.W), x(63, 0)), Cat(0.U(96.W), x(31, 0))),
      Mux(u.op === Ops.MOVQ.U, Cat(0.U(64.W), x(63, 0)),
        Mux(u.op === Ops.MOVPS.U && u.simd === SimdKind.SS.U, Cat(0.U(96.W), x(31, 0)),
          Mux(u.op === Ops.MOVPS.U && u.simd === SimdKind.SD.U, Cat(0.U(64.W), x(63, 0)), x))))

  private val loadXmmValue = sseLoadValue(io.dmem.resp.rdata, exMem.uop)

  val sseMem = Module(new SseAlu)
  sseMem.io.op   := exMem.uop.op
  sseMem.io.simd := exMem.uop.simd
  sseMem.io.opsz := exMem.uop.opsz
  sseMem.io.a    := exMem.xmmA
  sseMem.io.b    := io.dmem.resp.rdata
  sseMem.io.imm  := exMem.uop.imm(7, 0)
  sseMem.io.flagsIn := exMem.flags

  private val aluMemSrcEx = exMem.valid && exMem.uop.b.kind === OperandKind.Mem.U &&
    (exMem.uop.op === Ops.ADD.U || exMem.uop.op === Ops.SUB.U ||
      exMem.uop.op === Ops.AND.U || exMem.uop.op === Ops.OR.U ||
      exMem.uop.op === Ops.XOR.U || exMem.uop.op === Ops.CMP.U ||
      exMem.uop.op === Ops.TEST.U || exMem.uop.op === Ops.ADC.U ||
      exMem.uop.op === Ops.SBB.U)
  val aluRmw = Module(new IntAlu)
  aluRmw.io.op      := Mux(exMem.uop.op === Ops.XADD.U, Ops.ADD.U, exMem.uop.op)
  aluRmw.io.opsz    := exMem.uop.opsz
  aluRmw.io.a       := loadData
  aluRmw.io.b       := exMem.bVal
  aluRmw.io.hiIn    := 0.U
  aluRmw.io.imm     := exMem.uop.imm
  aluRmw.io.addrIn  := 0.U
  aluRmw.io.flagsIn := exMem.flags
  aluRmw.io.cond    := exMem.uop.cond
  aluRmw.io.oneOp   := false.B

  val aluMem = Module(new IntAlu)
  aluMem.io.op     := exMem.uop.op
  aluMem.io.opsz   := exMem.uop.opsz
  aluMem.io.a      := exMem.oldDst
  aluMem.io.b      := loadData
  aluMem.io.hiIn   := 0.U
  aluMem.io.imm    := exMem.uop.imm
  aluMem.io.addrIn := 0.U
  aluMem.io.flagsIn := exMem.flags
  aluMem.io.cond   := exMem.uop.cond
  aluMem.io.oneOp  := false.B

  private val sseMemMerge = Mux(exMem.uop.simd === SimdKind.SS.U,
    Cat(exMem.xmmA(127, 32), sseMem.io.result(31, 0)),
    Mux(exMem.uop.simd === SimdKind.SD.U,
      Cat(exMem.xmmA(127, 64), sseMem.io.result(63, 0)), sseMem.io.result))

  private val isSseComisMem = exMem.valid && isSse(exMem.uop) &&
    exMem.uop.b.kind === OperandKind.Mem.U &&
    (exMem.uop.op === Ops.SIMD_COMI.U || exMem.uop.op === Ops.SIMD_UCOMI.U)

  private val isSseArithMem = exMem.valid && isSse(exMem.uop) &&
    exMem.uop.b.kind === OperandKind.Mem.U &&
    (exMem.uop.op === Ops.SIMD_ADD.U || exMem.uop.op === Ops.SIMD_SUB.U ||
      exMem.uop.op === Ops.SIMD_MUL.U || exMem.uop.op === Ops.SIMD_DIV.U ||
      exMem.uop.op === Ops.SIMD_SQRT.U || exMem.uop.op === Ops.SIMD_MIN.U ||
      exMem.uop.op === Ops.SIMD_MAX.U || isSseComisMem)

  when(memResp) {
    memWait := false.B
    when(exMem.isStore && (exMem.addr === HaltMmio)) {
      haltedReg := true.B
      exitReg   := exMem.storeData(63, 0)
      pc        := exMem.uop.pc + exMem.uop.len
    }
  }

  // ---------------------------------------------------------- EX -> MEM latch
  private val exLatch = !memWait && !exHasMem && !(idEx.valid && (idEx.uop.op === Ops.LIDT.U || idEx.uop.op === Ops.IRET.U ||
      idEx.uop.op === Ops.CPUID.U || idEx.uop.op === Ops.INVLPG.U))

  when(exLatch) {
    exMem.valid   := idEx.valid
    exMem.uop     := idEx.uop
    exMem.result  := Mux(idEx.uop.op === Ops.RDMSR.U, msrVal(31, 0),
      Mux(idEx.uop.op === Ops.MOV_CR.U && !isCrWrite, crVal,
      Mux(isSse(idEx.uop) && idEx.uop.a.kind === OperandKind.Gpr.U,
        sse.io.gprResult, alu.io.result)))
    exMem.hi      := Mux(idEx.uop.op === Ops.RDMSR.U, msrVal(63, 32), alu.io.hi)
    exMem.flags   := Mux(idEx.uop.op === Ops.SIMD_UCOMI.U || idEx.uop.op === Ops.SIMD_COMI.U,
      sse.io.flags, Mux(isSpecial, specialFlags, alu.io.flags))
    exMem.wf      := alu.io.writesFlags || isSpecial ||
      (idEx.uop.op === Ops.POPF.U) ||
      (idEx.uop.op === Ops.SIMD_UCOMI.U || idEx.uop.op === Ops.SIMD_COMI.U)
    exMem.addr    := Mux(
      idEx.uop.op === Ops.PUSH.U || idEx.uop.op === Ops.CALL.U || idEx.uop.op === Ops.PUSHF.U,
      exRsp - 8.U,
      Mux(idEx.uop.op === Ops.POP.U || idEx.uop.op === Ops.RET.U || idEx.uop.op === Ops.POPF.U,
        exRsp, ea))
    exMem.storeData := Mux(isSse(idEx.uop) && idEx.uop.a.kind === OperandKind.Mem.U, exXb,
      lut(idEx.uop.op, alu.io.result,
      Seq(
        Ops.PUSH  -> Mux(idEx.uop.b.kind === OperandKind.Imm.U, idEx.uop.imm, exA),
        Ops.PUSHF -> flagsEx,
        Ops.CALL  -> (idEx.uop.pc + idEx.uop.len),
        Ops.SETCC -> alu.io.result))
      )
    exMem.target  := target
    exMem.taken   := taken
    exMem.isLoad  := memLoadOf(idEx.uop)
    exMem.isStore := memStoreOf(idEx.uop)
    exMem.writeHi := Mux(idEx.uop.op === Ops.RDMSR.U, true.B, alu.io.writeHi)
    exMem.divErr  := alu.io.divError
    exMem.rsp     := exRsp
    exMem.rspNext := Mux(
      idEx.uop.op === Ops.PUSH.U || idEx.uop.op === Ops.CALL.U || idEx.uop.op === Ops.PUSHF.U,
      exRsp - 8.U,
      Mux(idEx.uop.op === Ops.POP.U || idEx.uop.op === Ops.POPF.U,
        exRsp + 8.U,
        Mux(idEx.uop.op === Ops.RET.U, exRsp + 8.U + idEx.uop.imm, exRsp)))
    exMem.oldDst  := exA
    exMem.bVal    := Mux(idEx.uop.b.kind === OperandKind.Imm.U, idEx.uop.imm, exB)
    rmwPhase := false.B
    exMem.xmmResult := sseMerge
    exMem.xmmA      := exXa
    exMem.bad     := idEx.bad || (isSse(idEx.uop) && !sse.io.valid)
    exMem.uop.memsz := lut(idEx.uop.op, idEx.uop.memsz,
      Seq(Ops.PUSH -> 8.U, Ops.POP -> 8.U, Ops.CALL -> 8.U, Ops.RET -> 8.U,
        Ops.PUSHF -> 8.U, Ops.POPF -> 8.U))
    idEx.valid := false.B
  }

  // ---------------------------------------------------------- MEM -> WB latch
  memWb.valid := false.B
  when(exMem.valid && !exHasMem) {
    memWb.valid   := true.B
    memWb.uop     := exMem.uop
    memWb.result  := Mux(aluMemSrcEx, aluMem.io.result, exMem.result)
    memWb.hi      := exMem.hi
    memWb.flags   := exMem.flags
    memWb.wf      := exMem.wf
    memWb.writeHi := exMem.writeHi
    memWb.rspNext := 0.U
    memWb.oldDst  := exMem.oldDst
    memWb.bad     := exMem.bad
    memWb.xmmResult := exMem.xmmResult
  }
  when(memResp && isRmw && !rmwPhase) {
    rmwResult := aluRmw.io.result
    rmwFlags  := aluRmw.io.flags
    rmwLoaded := loadData
    rmwPhase  := true.B
  }
  when(memResp && (!isRmw || rmwPhase)) {
    memWb.valid   := true.B
    memWb.uop     := exMem.uop
    memWb.result  := Mux(isRmw && (exMem.uop.op === Ops.XCHG.U ||
      exMem.uop.op === Ops.XADD.U), rmwLoaded,
      Mux(isRmw, rmwResult,
      Mux(aluMemSrcEx, aluMem.io.result,
        Mux(exMem.isLoad, loadValue, exMem.result))))
    memWb.hi      := exMem.hi
    memWb.flags   := Mux(isRmw, rmwFlags,
      Mux(isSseComisMem, sseMem.io.flags,
        Mux(aluMemSrcEx, aluMem.io.flags,
          Mux(exMem.uop.op === Ops.POPF.U, loadData, exMem.flags))))
    memWb.wf      := (isRmw && exMem.uop.op =/= Ops.XCHG.U) || exMem.wf || exMem.uop.op === Ops.POPF.U || isSseComisMem ||
      (aluMemSrcEx && aluMem.io.writesFlags)
    memWb.writeHi := exMem.writeHi
    memWb.oldDst  := exMem.oldDst
    memWb.bad     := exMem.bad
    memWb.xmmResult := Mux(isSseArithMem, sseMemMerge,
      Mux(exMem.isLoad, loadXmmValue, exMem.xmmResult))
    memWb.rspNext := exMem.rspNext
    when(exHasMem) { exMem.valid := false.B }
  }

  when(exMem.valid && exMem.bad && !haltedReg) {
    trapReq := true.B
    trapReqVec := 6.U
    trapReqPc := exMem.uop.pc
  }
  when(exMem.valid && (exMem.uop.op === Ops.UD2.U) && !haltedReg) {
    trapReq := true.B
    trapReqVec := 6.U
    trapReqPc := exMem.uop.pc
  }
  when(exMem.valid && (exMem.uop.op === Ops.INT3.U) && !haltedReg) {
    trapReq := true.B
    trapReqVec := 3.U
    trapReqPc := exMem.uop.pc + exMem.uop.len
  }
  when(exMem.valid && (exMem.uop.op === Ops.INT.U) && !haltedReg) {
    trapReq := true.B
    trapReqVec := exMem.uop.imm(7, 0)
    trapReqPc := exMem.uop.pc + exMem.uop.len
  }
  when(exMem.valid && exMem.divErr && !haltedReg) {
    trapReq := true.B
    trapReqVec := 0.U
    trapReqPc := exMem.uop.pc
  }

  when(trapReq && !sysActive) {
    sysActive := true.B
    sysOp := 0.U
    sysStep := 0.U
    sysWait := false.B
    trapVecR := trapReqVec
    trapErrR := trapReqErr
    trapPcR := trapReqPc
    trapRspR := gpr.io.rs7Data
    idEx.valid := false.B
    exMem.valid := false.B
    memWb.valid := false.B
    memWait := false.B
    xlatReady := false.B
    ibufValid := false.B
    fState := 0.U
  }

  // ---------------------------------------------------------------------- WB
  private val wbOp = memWb.uop

  private val writesRax = memWb.valid && memWb.writeHi && wbOp.op =/= Ops.CDQ.U
  private val writesRdx = memWb.valid && memWb.writeHi

  private val wrDst = memWb.valid && !writesRax &&
    (wbOp.a.kind === OperandKind.Gpr.U ||
      ((wbOp.op === Ops.XCHG.U || wbOp.op === Ops.XADD.U) &&
        wbOp.b.kind === OperandKind.Gpr.U)) &&
    wbOp.op =/= Ops.CMP.U && wbOp.op =/= Ops.TEST.U &&
    wbOp.op =/= Ops.JMP.U && wbOp.op =/= Ops.JCC.U &&
    wbOp.op =/= Ops.CALL.U && wbOp.op =/= Ops.RET.U && wbOp.op =/= Ops.PUSH.U &&
    wbOp.op =/= Ops.NOP.U && wbOp.op =/= Ops.HLT.U &&
    wbOp.op =/= Ops.PUSHF.U && wbOp.op =/= Ops.POPF.U &&
    wbOp.op =/= Ops.CLC.U && wbOp.op =/= Ops.STC.U && wbOp.op =/= Ops.CMC.U &&
    wbOp.op =/= Ops.CLD.U && wbOp.op =/= Ops.STD.U && wbOp.op =/= Ops.CLI.U &&
    wbOp.op =/= Ops.STI.U && wbOp.op =/= Ops.UD2.U && wbOp.op =/= Ops.SAHF.U &&
    wbOp.op =/= Ops.RDMSR.U && wbOp.op =/= Ops.WRMSR.U &&
    wbOp.op =/= Ops.SYSCALL.U && wbOp.op =/= Ops.SYSRET.U &&
    wbOp.op =/= Ops.CPUID.U && wbOp.op =/= Ops.INVLPG.U

  private val stackOp = wbOp.op === Ops.PUSH.U || wbOp.op === Ops.POP.U ||
    wbOp.op === Ops.CALL.U || wbOp.op === Ops.RET.U ||
    wbOp.op === Ops.PUSHF.U || wbOp.op === Ops.POPF.U

  private def mergeValue(old: UInt, res: UInt, sz: UInt): UInt =
    Mux(sz === 1.U, (old & "hffffffffffffff00".U(64.W)) | (res & 0xff.U),
      Mux(sz === 2.U, (old & "hffffffffffff0000".U(64.W)) | (res & 0xffff.U),
        Mux(sz === 4.U, res & "hffffffff".U(64.W), res)))

  private val wbValue = mergeValue(memWb.oldDst, memWb.result, wbOp.opsz)
  private val dstIdx = Mux(writesRax, 0.U,
    Mux((wbOp.op === Ops.XCHG.U || wbOp.op === Ops.XADD.U) &&
      wbOp.a.kind === OperandKind.Mem.U, wbOp.b.idx, wbOp.a.idx))

  private val wrXmm = memWb.valid && wbOp.a.kind === OperandKind.Xmm.U &&
    wbOp.op =/= Ops.SIMD_UCOMI.U && wbOp.op =/= Ops.SIMD_COMI.U

  xmm.io.we    := wrXmm
  xmm.io.waddr := wbOp.a.idx
  xmm.io.wdata := memWb.xmmResult

  private val trapFinal = sysActive && sysOp === 0.U && sysStep === 8.U
  private val iretFinal = sysActive && sysOp === 2.U && sysStep === 5.U
  private val trapRspNext = trapRspR - Mux(trapHasErr, 48.U, 40.U)

  when(trapFinal) {
    pc := trapHandlerR
    flagsReg := flagsReg & ~(1.U << FlagBit.IF)
    sysActive := false.B
  }
  when(iretFinal) {
    pc := trapHandlerR
    flagsReg := iretFlags | "h2".U
    sysActive := false.B
  }
  when(sysActive && sysFinalLidt) {
    sysActive := false.B
  }
  when(sysActive && sysOp === 3.U && sysStep === 2.U) {
    sysActive := false.B
  }

  private val syscallWr = idEx.valid && isSyscall
  private val syscallNext = idEx.uop.pc + idEx.uop.len
  private val cpuid0 = sysActive && sysOp === 3.U && sysStep === 0.U
  private val cpuid1 = sysActive && sysOp === 3.U && sysStep === 1.U

  gpr.io.we1    := (memWb.valid && (wrDst || writesRax)) || trapFinal || iretFinal ||
    syscallWr || cpuid0 || cpuid1
  gpr.io.waddr1 := Mux(cpuid0, 0.U, Mux(cpuid1, 3.U,
    Mux(syscallWr, 1.U, Mux(iretFinal, 4.U, Mux(trapFinal, 4.U, dstIdx)))))
  gpr.io.wdata1 := Mux(cpuid0, cpuidEax.pad(64), Mux(cpuid1, cpuidEbx.pad(64),
    Mux(syscallWr, syscallNext,
      Mux(iretFinal, iretRsp, Mux(trapFinal, trapRspNext, wbValue)))))
  gpr.io.we2    := (memWb.valid && (writesRdx || stackOp)) || syscallWr || cpuid0 || cpuid1
  gpr.io.waddr2 := Mux(cpuid0, 2.U, Mux(cpuid1, 1.U,
    Mux(syscallWr, 11.U, Mux(writesRdx, 2.U, 4.U))))
  gpr.io.wdata2 := Mux(cpuid0, cpuidEdx.pad(64), Mux(cpuid1, cpuidEcx.pad(64),
    Mux(syscallWr, flagsReg, Mux(writesRdx, memWb.hi, memWb.rspNext))))

  when(memWb.valid && memWb.wf) { flagsReg := memWb.flags }
  when(memWb.valid && wbOp.op === Ops.HLT.U) { haltedReg := true.B }
  when(memWb.valid && (wbOp.op === Ops.UD2.U || wbOp.illegal)) {
    haltedReg := true.B
    fatalReg  := true.B
    fatalPcReg := wbOp.pc
    fatalCdReg := ExceptionCause.Ud.U
  }

  io.commit.valid := memWb.valid
  io.commit.pc    := wbOp.pc
  io.commit.len   := wbOp.len
  io.commit.rd    := dstIdx
  io.commit.wdata := wbValue
  io.commit.wreg  := memWb.valid && (wrDst || writesRax)
  io.commit.halted := haltedReg
  io.commit.exitCode := exitReg
  io.commit.flags := memWb.flags
  io.commit.op := wbOp.op
  io.commit.writesFlags := memWb.wf

  io.halted    := haltedReg
  io.exitCode  := exitReg
  io.fatal     := fatalReg
  io.fatalPc   := fatalPcReg
  io.fatalCode := fatalCdReg

  // ------------------------------------------------------------- front-end ctl
  private val frontStall = !canUse || memWait || exHasMem || sysActive || stopFetch
  private val exRedirect = idEx.valid && taken
  private val idExFree   = !idEx.valid || exLatch

  when(!frontStall && !exRedirect && !haltedReg && idExFree) {
    when(dec.io.illegal) {
      haltedReg  := true.B
      fatalReg   := true.B
      fatalPcReg := pc
      fatalCdReg := ExceptionCause.Ud.U
    }.otherwise {
      idEx.valid := true.B
      idEx.uop   := d
      idEx.bad   := unsupported
      pc := pc + d.len
      when(d.op === Ops.HLT.U) { stopFetch := true.B }
    }
  }

  when(exRedirect) {
    pc := target
    ibufValid := false.B
    idEx.valid := false.B
  }

  when(memResp && (exMem.uop.op === Ops.RET.U ||
    (exMem.uop.op === Ops.JMP.U && exMem.uop.a.kind === OperandKind.Mem.U))) {
    pc := loadData
    ibufValid := false.B
    idEx.valid := false.B
  }

  // -------------------------------------------------- fetch address translation
  // The fetch path shares the data MMU. Data translations always win the
  // arbiter: a fetch translation is only granted while the MMU is free and the
  // data path is not requesting. A data access that shows up while a fetch walk
  // is already in flight waits for that walk to finish (the walk cannot be
  // aborted); this is rare because a fetch walk only starts when the data path
  // is idle.
  //
  // Only the 16B-aligned line base is translated. A 32B fetch window can
  // straddle a 4 KiB page boundary and would need two translations, so the
  // tests keep every fetch window inside a single page.
  private val dataWantsMmu  = needXlat
  private val mmuFree       = !mmu.io.busy && (mmuOwner === 0.U)
  private val fetchWantsMmu = (fState === 0.U) && needLine && cr0(31)
  private val fetchGrant    = fetchWantsMmu && !dataWantsMmu && mmuFree
  private val dataGrant     = dataWantsMmu && mmuFree
  private val fetchActive   = fetchGrant || (mmuOwner === 2.U)

  // Do not keep reqValid high while a fetch walk owns the MMU; otherwise the
  // MMU cannot leave its done state and the data request would deadlock.
  mmu.io.reqValid := (dataWantsMmu && mmuOwner =/= 2.U) || fetchGrant
  mmu.io.vaddr    := Mux(fetchGrant, lineBase, Mux(fetchActive, fetchVaddr, exMem.addr))
  mmu.io.isWrite  := Mux(fetchActive, false.B, exMem.isStore)
  mmu.io.isFetch  := fetchActive

  when(dataGrant) { mmuOwner := 1.U }
  when(mmu.io.done && (mmuOwner === 1.U || mmuOwner === 2.U)) { mmuOwner := 0.U }
  when(fetchGrant) {
    fetchVaddr := lineBase
    mmuOwner   := 2.U
  }

  // A redirect while the walk is in flight means the translation is for a line
  // that will no longer be fetched; drop it instead of reporting a stale fault.
  private val fetchRedirect = exRedirect || (memResp && exMem.uop.op === Ops.RET.U)

  io.imem.req.valid := fState === 2.U
  io.imem.req.addr  := fetchPaddr

  when(fState === 0.U) {
    when(!cr0(31)) {
      // Paging off: identity map, skip the MMU entirely.
      when(needLine) {
        fetchVaddr := lineBase
        fetchPaddr := lineBase
        fState     := 2.U
      }
    }.elsewhen(fetchGrant) {
      // Inputs were accepted by the MMU this cycle; wait for done in state 1.
      fState := 1.U
    }
  }
  when(fState === 1.U) {
    when(mmu.io.done && mmuOwner === 2.U) {
      when(fetchRedirect || (fetchVaddr =/= lineBase)) {
        fState := 0.U
      }.elsewhen(mmu.io.isGp) {
        trapReq    := true.B
        trapReqVec := 13.U
        trapReqErr := 0.U
        trapReqPc  := fetchVaddr
        fState     := 0.U
      }.elsewhen(mmu.io.fault) {
        // Instruction fetch page fault: vector 14, error code bit4 = I/D.
        trapReq    := true.B
        trapReqVec := 14.U
        trapReqErr := mmu.io.error.pad(32)
        trapReqPc  := fetchVaddr
        fState     := 0.U
      }.otherwise {
        fetchPaddr := mmu.io.paddr
        fState     := 2.U
      }
    }
  }
  when(fState === 2.U) { fState := 3.U }
  when(fState === 3.U && io.imem.resp.valid) {
    ibuf      := io.imem.resp.data
    ibufBase  := fetchVaddr
    ibufValid := true.B
    fState    := 0.U
  }

  for (i <- 0 until 16) { io.dbg.gpr(i) := gpr.io.dbg(i) }
  io.dbg.rip    := pc
  io.dbg.flags  := flagsReg
  io.dbg.cycles := cycleCnt
  io.dbg.exValid := idEx.valid
  io.dbg.exPc := idEx.uop.pc
  io.dbg.exOp := idEx.uop.op
  io.dbg.exAIdx := idEx.uop.a.idx
  io.dbg.exA := exA
  io.dbg.exB := exB
  io.dbg.aluA := aluA
  io.dbg.aluB := aluB
  for (i <- 0 until 16) { io.dbg.xmm(i) := xmm.io.dbg(i) }
}
