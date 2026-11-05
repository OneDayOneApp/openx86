// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tongxiaojun

package x86.ooo

import chisel3._
import chisel3.util._
import x86.isa._
import x86.X86Config
import x86.frontend.Decoder
import x86.core.{IntAlu, SseAlu, Mmu}

/**
 * Stage-B out-of-order core (v1).
 *
 * In-order fetch/decode/rename/dispatch, distributed-free unified issue queue,
 * a single ALU port, physical register files for GPR and flags, and a ROB with
 * per-branch checkpoints. Memory, SSE and stack operations are rejected for now
 * (fatal) so the OoO mechanics can be validated against the Stage-A core.
 */
class OooCore extends Module {
  val io = IO(new Bundle {
    val imem = new Bundle {
      val req  = Output(UInt(64.W))
      val valid = Output(Bool())
      val resp = Input(UInt(256.W))
      val respValid = Input(Bool())
    }
    val dmem = new Bundle {
      val reqValid = Output(Bool())
      val reqWe    = Output(Bool())
      val reqAddr  = Output(UInt(64.W))
      val reqWdata = Output(UInt(128.W))
      val reqWmask = Output(UInt(16.W))
      val reqSize  = Output(UInt(5.W))
      val respValid = Input(Bool())
      val respData  = Input(UInt(128.W))
    }
    val ptw = new Bundle {
      val reqValid = Output(Bool())
      val reqAddr  = Output(UInt(64.W))
      val respData = Input(UInt(64.W))
      val respValid = Input(Bool())
    }
    val iptw = new Bundle {
      val reqValid = Output(Bool())
      val reqAddr  = Output(UInt(64.W))
      val respData = Input(UInt(64.W))
      val respValid = Input(Bool())
    }
    val halted    = Output(Bool())
    val exitCode  = Output(UInt(64.W))
    val fatal     = Output(Bool())
    val fatalPc   = Output(UInt(64.W))
    val fatalCode = Output(UInt(8.W))
    val dbg = Output(new Bundle {
      val gpr    = Vec(16, UInt(64.W))
      val xmm    = Vec(16, UInt(128.W))
      val flags  = UInt(64.W)
      val rip    = UInt(64.W)
      val cycles = UInt(64.W)
      val headSeq = UInt(32.W)
      val tailSeq = UInt(32.W)
      val iqUsed  = UInt(8.W)
      val canUse  = Bool()
      val dispatch = Bool()
      val issue    = Bool()
      val busyLow  = UInt(32.W)
      val iqReady  = UInt(8.W)
      val issuePc  = UInt(64.W)
      val issueRob = UInt(5.W)
      val iqInfo = Vec(8, UInt(32.W))
      val headValid = Bool()
      val headDone  = Bool()
      val headPc    = UInt(64.W)
      val headEntrySeq = UInt(32.W)
      val mqCountDbg = UInt(4.W)
      val mqHeadDbg  = UInt(3.W)
      val mqTailDbg  = UInt(3.W)
      val meValidDbg = Bool()
      val mePcDbg    = UInt(64.W)
      val meStoreDbg = Bool()
      val ldWaitDbg  = Bool()
      val stWaitDbg  = Bool()
      val sbMaskDbg  = UInt(8.W)
      val flushTargetDbg = UInt(64.W)
      val mqInfoDbg = Vec(8, UInt(64.W))
      val meOpDbg = UInt(8.W)
      val headSizeDbg = UInt(5.W)
      val meSizeDbg = UInt(5.W)
      val headDataLoDbg = UInt(64.W)
      val decOpDbg = UInt(8.W)
      val decAKindDbg = UInt(3.W)
      val decBKindDbg = UInt(3.W)
      val decMoveDbg = Bool()
      val decSizeDbg = UInt(5.W)
      val cr0Dbg = UInt(64.W)
      val cr3Dbg = UInt(64.W)
      val movCrWriteDbg = Bool()
      val movCrValDbg = UInt(64.W)
      val meVaddrDbg = UInt(64.W)
      val mePaddrDbg = UInt(64.W)
      val xlatDbg = UInt(4.W)
      val ptwDataDbg = UInt(64.W)
      val ptwValidDbg = Bool()
      val mmuFaultDbg = Bool()
      val ptwAddrDbg = UInt(64.W)
      val divOpDbg = UInt(8.W)
      val divADbg = UInt(64.W)
      val divBDbg = UInt(64.W)
      val divHiDbg = UInt(64.W)
      val divOpszDbg = UInt(5.W)
      val divErrDbg = Bool()
      val decIllegalDbg = Bool()
      val canDispatchDbg = Bool()
      val isCdqDbg = Bool()
      val issue2Cnt = UInt(32.W)
      val issue1Dbg = Bool()
      val meJmpMemDbg = Bool()
      val anyFwdDbg = Bool()
      val loadReqDbg = Bool()
      val meReadyDbg = Bool()
      val meComisDbg = Bool()
      val meHasDstFDbg = Bool()
      val mePdstFDbg = UInt(4.W)
      val decNeedFDbg = Bool()
      val decPdstFDbg = UInt(4.W)
      val ratFDbg = UInt(4.W)
      val f0Dbg = UInt(64.W)
      val f1Dbg = UInt(64.W)
      val f2Dbg = UInt(64.W)
      val f3Dbg = UInt(64.W)
    })
  })

  private val RobSize = 32
  private val IqSize  = 8
  private val NumPgpr = 64
  private val NumPx   = 32
  private val NumPf   = 16
  private val HaltMmio = X86Config.HaltMmio.U(64.W)

  // ------------------------------------------------------------------ helpers
  private def isAluOp(op: UInt): Bool =
    op === Ops.MOV.U || op === Ops.MOVZX.U || op === Ops.MOVSX.U || op === Ops.MOVSXD.U ||
      op === Ops.LEA.U || op === Ops.ADD.U || op === Ops.OR.U || op === Ops.ADC.U ||
      op === Ops.SBB.U || op === Ops.AND.U || op === Ops.SUB.U || op === Ops.XOR.U ||
      op === Ops.CMP.U || op === Ops.TEST.U || op === Ops.INC.U || op === Ops.DEC.U ||
      op === Ops.NEG.U || op === Ops.NOT.U || op === Ops.SHL.U || op === Ops.SHR.U ||
      op === Ops.SAR.U || op === Ops.ROL.U || op === Ops.ROR.U || op === Ops.RCL.U ||
      op === Ops.RCR.U || op === Ops.SETCC.U || op === Ops.CMOVCC.U ||
      op === Ops.XCHG.U || op === Ops.JCC.U || op === Ops.JMP.U ||
      op === Ops.HLT.U || op === Ops.NOP.U || op === Ops.ENDBR.U ||
      op === Ops.MUL.U || op === Ops.IMUL.U || op === Ops.DIV.U || op === Ops.IDIV.U ||
      op === Ops.LOOPNE.U || op === Ops.LOOPE.U || op === Ops.JRCXZ.U

  private def writesFlagsOp(op: UInt): Bool =
    op === Ops.ADD.U || op === Ops.OR.U || op === Ops.ADC.U || op === Ops.SBB.U ||
      op === Ops.AND.U || op === Ops.SUB.U || op === Ops.XOR.U || op === Ops.CMP.U ||
      op === Ops.TEST.U || op === Ops.INC.U || op === Ops.DEC.U || op === Ops.NEG.U ||
      op === Ops.SHL.U || op === Ops.SHR.U || op === Ops.SAR.U || op === Ops.ROL.U ||
      op === Ops.ROR.U || op === Ops.RCL.U || op === Ops.RCR.U

  private def needsFlagsOp(op: UInt): Bool =
    op === Ops.JCC.U || op === Ops.CMOVCC.U || op === Ops.SETCC.U ||
      op === Ops.ADC.U || op === Ops.SBB.U || op === Ops.RCL.U || op === Ops.RCR.U

  private def isSseOp(op: UInt): Bool = op >= Ops.MOVD.U && op <= Ops.CVTTPD2DQ.U
  private def isSseMoveOp(op: UInt): Bool =
    op === Ops.MOVD.U || op === Ops.MOVQ.U || op === Ops.MOVDQ.U ||
      op === Ops.MOVPS.U

  private def isComisOp(op: UInt): Bool =
    op === Ops.SIMD_COMI.U || op === Ops.SIMD_UCOMI.U

  private def writesGprOp(u: Uop): Bool =
    u.a.kind === OperandKind.Gpr.U &&
      (u.op === Ops.MOV.U || u.op === Ops.MOVZX.U || u.op === Ops.MOVSX.U ||
        u.op === Ops.MOVSXD.U || u.op === Ops.LEA.U || u.op === Ops.ADD.U ||
        u.op === Ops.OR.U || u.op === Ops.ADC.U || u.op === Ops.SBB.U ||
        u.op === Ops.AND.U || u.op === Ops.SUB.U || u.op === Ops.XOR.U ||
        u.op === Ops.INC.U || u.op === Ops.DEC.U || u.op === Ops.NEG.U ||
        u.op === Ops.NOT.U || u.op === Ops.SHL.U || u.op === Ops.SHR.U ||
        u.op === Ops.SAR.U || u.op === Ops.ROL.U || u.op === Ops.ROR.U ||
        u.op === Ops.RCL.U || u.op === Ops.RCR.U || u.op === Ops.SETCC.U ||
        u.op === Ops.CMOVCC.U || u.op === Ops.XCHG.U)

  // -------------------------------------------------------------------- state
  class RobEntry extends Bundle {
    val valid  = Bool()
    val seq    = UInt(32.W)
    val uop    = new Uop
    val hasDst = Bool()
    val pdst   = UInt(6.W)
    val oldDst = UInt(6.W)
    val hasDstF = Bool()
    val pdstF  = UInt(4.W)
    val oldDstF = UInt(4.W)
    val hasDstX = Bool()
    val pdstX  = UInt(5.W)
    val oldDstX = UInt(5.W)
    val hasDst2 = Bool()
    val pdst2  = UInt(6.W)
    val oldDst2 = UInt(6.W)
    val done   = Bool()
    val value  = UInt(64.W)
    val flagsVal = UInt(64.W)
    val taken  = Bool()
    val target = UInt(64.W)
    val isBranch = Bool()
    val predTaken = Bool()
    val isStore = Bool()
    val memAddr = UInt(64.W)
    val memData = UInt(128.W)
    val memSize = UInt(5.W)
    val storeDone = Bool()
    val cpRatG  = Vec(16, UInt(6.W))
    val cpRatF  = Vec(16, UInt(4.W))
    val cpRatX  = Vec(16, UInt(5.W))
    val cpFreeG = Vec(64, Bool())
    val cpFreeF = Vec(16, Bool())
    val cpBusyG = Vec(64, Bool())
    val cpBusyF = Vec(16, Bool())
  }

  class IqEntry extends Bundle {
    val valid = Bool()
    val seq   = UInt(32.W)
    val uop   = new Uop
    val robIdx = UInt(5.W)
    val psrcA = UInt(6.W)
    val psrcB = UInt(6.W)
    val psrcF = UInt(4.W)
    val psrcXA = UInt(5.W)
    val psrcXB = UInt(5.W)
    val psrcC = UInt(6.W)
    val psrcBase = UInt(6.W)
    val psrcIdx  = UInt(6.W)
    val needA = Bool()
    val needB = Bool()
    val needF = Bool()
    val needXA = Bool()
    val needXB = Bool()
    val needC  = Bool()
    val needBase = Bool()
    val needIdx  = Bool()
    val hasDst  = Bool()
    val pdst    = UInt(6.W)
    val hasDstF = Bool()
    val pdstF   = UInt(4.W)
    val hasDstX = Bool()
    val pdstX   = UInt(5.W)
    val hasDst2 = Bool()
    val pdst2   = UInt(6.W)
    val isBranch = Bool()
    val predTaken = Bool()
    val target = UInt(64.W)
  }

  class MqEntry extends Bundle {
    val valid = Bool()
    val seq = UInt(32.W)
    val uop = new Uop
    val robIdx = UInt(5.W)
    val psrcXA = UInt(5.W)
    val psrcXB = UInt(5.W)
    val psrcBase = UInt(6.W)
    val psrcIdx  = UInt(6.W)
    val psrcData = UInt(6.W)
    val needBase = Bool()
    val needIdx  = Bool()
    val needData = Bool()
    val psrcA   = UInt(6.W)
    val needA   = Bool()
    val isAluMem = Bool()
    val isAluRmw = Bool()
    val psrcB    = UInt(6.W)
    val srcBImm  = UInt(64.W)
    val hasDst = Bool()
    val pdst   = UInt(6.W)
    val oldDst = UInt(6.W)
    val isStore = Bool()
    val isPush  = Bool()
    val isPop   = Bool()
    val isCall  = Bool()
    val isCallReg = Bool()
    val isRet   = Bool()
    val isJmpMem = Bool()
    val isComisMem = Bool()
    val psrcTarget = UInt(6.W)
    val needTarget = Bool()
    val isPushF = Bool()
    val isPopF  = Bool()
    val hasDstF = Bool()
    val pdstF   = UInt(4.W)
    val psrcF   = UInt(4.W)
    val isSseMem = Bool()
    val isSseArith = Bool()
    val sseStore = Bool()
    val hasDstX = Bool()
    val pdstX   = UInt(5.W)
    val oldDstX = UInt(5.W)
    val needXB  = Bool()
    val psrcRsp = UInt(6.W)
    val pdst2   = UInt(6.W)
    val hasDst2 = Bool()
    val memSize = UInt(4.W)
  }

  class SbEntry extends Bundle {
    val valid = Bool()
    val seq = UInt(32.W)
    val addr = UInt(64.W)
    val data = UInt(128.W)
    val size = UInt(5.W)
    val robIdx = UInt(5.W)
  }

  val rob = RegInit(VecInit(Seq.fill(RobSize)(0.U.asTypeOf(new RobEntry))))
  val mq  = RegInit(VecInit(Seq.fill(8)(0.U.asTypeOf(new MqEntry))))
  val sb  = RegInit(VecInit(Seq.fill(8)(0.U.asTypeOf(new SbEntry))))
  val mqHead = RegInit(0.U(3.W))
  val mqTail = RegInit(0.U(3.W))
  val mqCount = RegInit(0.U(4.W))
  val mqPush = WireDefault(false.B)
  val mqPop  = WireDefault(false.B)
  val ldWait = RegInit(false.B)
  val stWait = RegInit(false.B)
  val ldRobIdx = RegInit(0.U(5.W))
  val stRobIdx = RegInit(0.U(5.W))
  val ldSeq = RegInit(0.U(32.W))
  val ldIsPop = RegInit(false.B)
  val ldIsRet = RegInit(false.B)
  val ldIsPopF = RegInit(false.B)
  val xlWait = RegInit(false.B)
  val xlDone = RegInit(false.B)
  val xlPaddr = Reg(UInt(64.W))
  val stSeq = RegInit(0.U(32.W))
  val iq  = RegInit(VecInit(Seq.fill(IqSize)(0.U.asTypeOf(new IqEntry))))

  val ratG = RegInit(VecInit((0 until 16).map(i => i.U(6.W))))
  val ratF = RegInit(VecInit(Seq.fill(16)(0.U(4.W))))
  val ratX = RegInit(VecInit((0 until 16).map(i => i.U(5.W))))
  val freeG = RegInit(VecInit((0 until 64).map(i => (i >= 16).B)))
  val freeF = RegInit(VecInit((0 until 16).map(i => (i >= 1).B)))
  val freeX = RegInit(VecInit((0 until 32).map(i => (i >= 16).B)))
  val busyG = RegInit(VecInit(Seq.fill(64)(false.B)))
  val busyF = RegInit(VecInit(Seq.fill(16)(false.B)))
  val busyX = RegInit(VecInit(Seq.fill(32)(false.B)))

  val prfG = Reg(Vec(NumPgpr, UInt(64.W)))
  val prfX = Reg(Vec(NumPx, UInt(128.W)))
  val prfF = RegInit(VecInit((0 until NumPf).map(i => (if (i == 0) "h202".U(64.W) else 0.U(64.W)))))

  val cr0 = RegInit(0.U(64.W))
  val cr2 = RegInit(0.U(64.W))
  val cr3 = RegInit(0.U(64.W))
  val cr4 = RegInit(0.U(64.W))
  private val pgOn = cr0(31)

  val headSeq = RegInit(0.U(32.W))
  val tailSeq = RegInit(0.U(32.W))

  val pc = RegInit(X86Config.ResetVector.U(64.W))
  val ibuf = Reg(UInt(256.W))
  val ibufBase = Reg(UInt(64.W))
  val ibufValid = RegInit(false.B)
  val fState = RegInit(0.U(2.W))
  val fetchPaddr = Reg(UInt(64.W))
  val fetchVaddr = Reg(UInt(64.W))
  val cr3Flush = RegInit(false.B)
  val fetchAddr = RegInit(0.U(64.W))
  val fetchStop = RegInit(false.B)

  val haltedReg = RegInit(false.B)
  val fatalReg  = RegInit(false.B)
  val fatalPcReg = RegInit(0.U(64.W))
  val fatalCdReg = RegInit(0.U(8.W))
  val cycleCnt = RegInit(0.U(64.W))
  val issue2Cnt = RegInit(0.U(32.W))
  cycleCnt := cycleCnt + 1.U

  // -------------------------------------------------------------------- fetch
  private val lineBase = Cat(pc(63, 4), 0.U(4.W))
  private val canUse = ibufValid && (pc >= ibufBase) && (pc < (ibufBase + 16.U))
  private val needLine = !canUse && !fetchStop && !haltedReg && !fatalReg && fState === 0.U

  val immu = Module(new Mmu)
  immu.io.vaddr   := lineBase
  immu.io.isWrite := false.B
  immu.io.isFetch := true.B
  immu.io.pg      := pgOn
  immu.io.wp      := cr0(16)
  immu.io.nxe     := false.B
  immu.io.cr3     := cr3
  immu.io.flush   := cr3Flush
  immu.io.ptwData := io.iptw.respData
  immu.io.ptwValid := io.iptw.respValid
  immu.io.reqValid := fState === 0.U && needLine && pgOn
  private val iptwReqReg  = RegNext(immu.io.ptwReq)
  private val iptwAddrReg = RegNext(immu.io.ptwAddr)
  io.iptw.reqValid := iptwReqReg
  io.iptw.reqAddr  := iptwAddrReg

  when(fState === 0.U) {
    when(needLine) {
      when(!pgOn) {
        fetchPaddr := lineBase
        fetchVaddr := lineBase
        fState := 2.U
      }.otherwise {
        fState := 1.U
      }
    }
  }
  when(fState === 1.U) {
    when(immu.io.done) {
      when(immu.io.fault || immu.io.isGp) {
        haltedReg := true.B
        fatalReg := true.B
        fatalPcReg := pc
        fatalCdReg := ExceptionCause.Pf.U
        fState := 0.U
      }.otherwise {
        fetchPaddr := immu.io.paddr
        fetchVaddr := lineBase
        fState := 2.U
      }
    }
  }
  io.imem.valid := fState === 2.U
  io.imem.req := fetchPaddr
  when(fState === 2.U) { fState := 3.U }
  when(fState === 3.U && io.imem.respValid) {
    ibuf := io.imem.resp
    ibufBase := fetchVaddr
    ibufValid := true.B
    fState := 0.U
  }

  private val off = pc - ibufBase
  private val window = (ibuf >> (off << 3))(127, 0)
  private val decBytes = Wire(Vec(16, UInt(8.W)))
  for (i <- 0 until 16) { decBytes(i) := window(8 * i + 7, 8 * i) }

  val dec = Module(new Decoder)
  dec.io.pc := pc
  dec.io.bytes := decBytes
  private val d = dec.io.uop

  // -------------------------------------------------------------------- issue
  private val iqReady = Wire(Vec(IqSize, Bool()))
  for (i <- 0 until IqSize) {
    val e = iq(i)
    iqReady(i) := e.valid &&
      (!e.needA || !busyG(e.psrcA)) &&
      (!e.needB || !busyG(e.psrcB)) &&
      (!e.needF || !busyF(e.psrcF)) &&
      (!e.needXA || !busyX(e.psrcXA)) &&
      (!e.needXB || !busyX(e.psrcXB)) &&
      (!e.needC || !busyG(e.psrcC)) &&
      (!e.needBase || !busyG(e.psrcBase)) &&
      (!e.needIdx || !busyG(e.psrcIdx)) &&
      (!(e.uop.op === Ops.MOV_CR.U && e.uop.cond(0)) || e.seq === headSeq)
  }
  private val iqSel = Wire(Vec(IqSize, Bool()))
  for (i <- 0 until IqSize) {
    var older = true.B
    for (j <- 0 until IqSize) {
      if (i != j) {
        older = older && (!(iq(j).valid && iqReady(j)) || iq(i).seq <= iq(j).seq)
      }
    }
    iqSel(i) := iqReady(i) && older
  }
  private val issueValid = iqSel.asUInt.orR
  private val issueIdx = PriorityEncoder(iqSel.asUInt)
  private val ie = iq(issueIdx)

  // second issue port: eligible non-branch, non-serializing register ops
  private val port1Elig = Wire(Vec(IqSize, Bool()))
  for (i <- 0 until IqSize) {
    val op = iq(i).uop.op
    port1Elig(i) := iqReady(i) && !iq(i).isBranch &&
      op =/= Ops.MOV_CR.U && op =/= Ops.MUL.U && op =/= Ops.IMUL.U &&
      op =/= Ops.DIV.U && op =/= Ops.IDIV.U && op =/= Ops.CDQ.U && op =/= Ops.CBW.U
  }
  private val iqSel1 = Wire(Vec(IqSize, Bool()))
  for (i <- 0 until IqSize) {
    val older = iqReady.asUInt & iqSel.asUInt
    var cnt = 0.U(4.W)
    for (j <- 0 until IqSize) {
      if (i != j) {
        cnt = cnt +& (iqReady(j) && iq(j).seq < iq(i).seq).asUInt
      }
    }
    iqSel1(i) := port1Elig(i) && (cnt === 1.U)
  }
  private val issueValid1 = iqSel1.asUInt.orR
  private val issueIdx1 = PriorityEncoder(iqSel1.asUInt)
  private val ie1 = iq(issueIdx1)

  private val rsA = prfG(ie.psrcA)
  private val rsB = prfG(ie.psrcB)
  private val rsF = prfF(ie.psrcF)
  private val rsBase = prfG(ie.psrcBase)
  private val rsIdx = prfG(ie.psrcIdx)

  private def lut[T <: Data](k: UInt, d: T, m: Seq[(Any, T)]): T =
    m.foldRight(d) { case ((key, v), acc) =>
      val kk: UInt = key match {
        case i: Int  => i.U
        case u: UInt => u
        case other   => throw new IllegalArgumentException(s"bad key $other")
      }
      Mux(k === kk, v, acc)
    }

  private val srcBits = ie.uop.memsz << 3
  private val movExt = lut(ie.uop.op, rsB,
    Seq(
      Ops.MOVZX -> ({ val m = ((1.U(64.W) << srcBits) - 1.U)(63, 0); rsB & m }),
      Ops.MOVSX -> ({
        val m = ((1.U(64.W) << srcBits) - 1.U)(63, 0)
        val sign = (rsB >> (srcBits - 1.U))(0)
        (rsB & m) | Mux(sign, ~m, 0.U(64.W))
      }),
      Ops.MOVSXD -> ((rsB & "hffffffff".U) |
        Mux(rsB(31), "hffffffff00000000".U, 0.U(64.W)))
    ))

  private val aluB = Mux(ie.uop.b.kind === OperandKind.Imm.U, ie.uop.imm,
    Mux(ie.uop.op === Ops.MOVZX.U || ie.uop.op === Ops.MOVSX.U || ie.uop.op === Ops.MOVSXD.U,
      movExt, rsB))

  private val ea = WireDefault(0.U(64.W))
  when(ie.uop.op === Ops.LEA.U) {
    val baseVal = Mux(ie.uop.mem.ripRel, ie.uop.pc + ie.uop.len,
      Mux(ie.uop.mem.hasBase, rsBase, 0.U))
    ea := baseVal +
      Mux(ie.uop.mem.hasIndex, rsIdx << ie.uop.mem.scale, 0.U) + ie.uop.mem.disp
  }

  val alu = Module(new IntAlu)
  private val ieMulDiv = ie.uop.op === Ops.MUL.U || ie.uop.op === Ops.IMUL.U ||
    ie.uop.op === Ops.DIV.U || ie.uop.op === Ops.IDIV.U
  private val ieCdq = ie.uop.op === Ops.CDQ.U
  private val ieCbw = ie.uop.op === Ops.CBW.U
  alu.io.op := ie.uop.op
  alu.io.opsz := ie.uop.opsz
  alu.io.a := Mux(ieMulDiv, rsA, rsA)
  alu.io.b := Mux(ieMulDiv, rsB, aluB)
  alu.io.hiIn := Mux(ieMulDiv, prfG(ie.psrcC), 0.U)
  alu.io.imm := ie.uop.imm
  alu.io.addrIn := ea
  alu.io.flagsIn := rsF
  alu.io.cond := ie.uop.cond
  alu.io.oneOp := ieMulDiv

  val sse = Module(new SseAlu)
  sse.io.op := ie.uop.op
  sse.io.simd := ie.uop.simd
  sse.io.opsz := ie.uop.opsz
  sse.io.a := Mux(ie.needXA, prfX(ie.psrcXA),
    Mux(ie.uop.a.kind === OperandKind.Gpr.U, prfG(ie.psrcA).pad(128), 0.U(128.W)))
  sse.io.b := Mux(ie.needXB, prfX(ie.psrcXB),
    Mux(ie.uop.b.kind === OperandKind.Gpr.U,
      Mux(ie.uop.opsz === 4.U, prfG(ie.psrcB)(31, 0).pad(128), prfG(ie.psrcB).pad(128)),
      0.U(128.W)))
  sse.io.imm := ie.uop.imm(7, 0)
  sse.io.flagsIn := rsF

  private val wbVal = Mux(ie.uop.opsz === 1.U,
    (rsA & "hffffffffffffff00".U(64.W)) | (alu.io.result & "hff".U),
    Mux(ie.uop.opsz === 2.U,
      (rsA & "hffffffffffff0000".U(64.W)) | (alu.io.result & "hffff".U),
      Mux(ie.uop.opsz === 4.U, alu.io.result & "hffffffff".U(64.W), alu.io.result)))

  private val ieMovCr = ie.uop.op === Ops.MOV_CR.U
  private val ieMovCrWrite = ieMovCr && ie.uop.cond(0)
  private val ieMovCrRead = ieMovCr && !ie.uop.cond(0)
  private val crIdx = ie.uop.imm(3, 0)
  private val crVal = lut(crIdx, 0.U(64.W),
    Seq(0.U -> cr0, 2.U -> cr2, 3.U -> cr3, 4.U -> cr4))

  private val sseGprVal = Mux(ie.uop.opsz === 1.U,
    (rsA & "hffffffffffffff00".U) | (sse.io.gprResult & "hff".U),
    Mux(ie.uop.opsz === 2.U,
      (rsA & "hffffffffffff0000".U) | (sse.io.gprResult & "hffff".U),
      Mux(ie.uop.opsz === 4.U, sse.io.gprResult & "hffffffff".U(64.W), sse.io.gprResult)))

  private val ieLoopCc = ie.uop.op === Ops.LOOPNE.U || ie.uop.op === Ops.LOOPE.U
  private val ieJrcxz = ie.uop.op === Ops.JRCXZ.U
  private val zf = rsF(FlagBit.ZF)
  private val issueTaken = ie.uop.op === Ops.JMP.U ||
    (ie.uop.op === Ops.JCC.U && alu.io.branchTaken) ||
    (ie.uop.op === Ops.LOOPNE.U && (alu.io.result =/= 0.U) && !zf) ||
    (ie.uop.op === Ops.LOOPE.U && (alu.io.result =/= 0.U) && zf) ||
    (ieJrcxz && rsA === 0.U)
  private val issueMispredict = issueValid && ie.isBranch && (issueTaken =/= ie.predTaken)

  private val mqFlush = WireDefault(false.B)
  private val mqFlushRob = WireDefault(0.U(5.W))
  private val mqFlushSeq = WireDefault(0.U(32.W))
  private val mqFlushTarget = WireDefault(0.U(64.W))
  private val issueTarget = Mux(ieLoopCc || ieJrcxz,
    ie.uop.pc + ie.uop.len + ie.uop.imm,
    Mux(ie.uop.b.kind === OperandKind.Imm.U,
      ie.uop.pc + ie.uop.len + ie.uop.imm, rsA))

  when(issueValid) {
    iq(issueIdx).valid := false.B
    when(ie.hasDst) {
      prfG(ie.pdst) := Mux(ieCbw, alu.io.result,
        Mux(ieMovCrRead, crVal, Mux(isSseOp(ie.uop.op), sseGprVal, wbVal)))
      busyG(ie.pdst) := false.B
    }
    when(ie.hasDst2) {
      prfG(ie.pdst2) := alu.io.hi
      busyG(ie.pdst2) := false.B
    }
    when(ie.hasDstX) {
      prfX(ie.pdstX) := sse.io.result
      busyX(ie.pdstX) := false.B
    }
    when(ieMovCrWrite) {
      when(crIdx === 0.U) { cr0 := rsA }
      when(crIdx === 3.U) { cr3 := rsA; cr3Flush := true.B }
      when(crIdx === 4.U) { cr4 := rsA }
      mqFlush := true.B
      mqFlushRob := ie.robIdx
      mqFlushSeq := ie.seq
      mqFlushTarget := ie.uop.pc + ie.uop.len
    }
    when(ie.hasDstF) {
      prfF(ie.pdstF) := Mux(isSseOp(ie.uop.op), sse.io.flags, alu.io.flags)
      busyF(ie.pdstF) := false.B
    }
    rob(ie.robIdx).done := true.B
    rob(ie.robIdx).value := Mux(isSseOp(ie.uop.op), sseGprVal, wbVal)
    when(ieMulDiv && alu.io.divError) {
      haltedReg := true.B
      fatalReg := true.B
      fatalPcReg := ie.uop.pc
      fatalCdReg := ExceptionCause.De.U
    }
    rob(ie.robIdx).flagsVal := alu.io.flags
    rob(ie.robIdx).taken := issueTaken
    rob(ie.robIdx).target := issueTarget

    // branch mispredict
    when(ie.isBranch && (issueTaken =/= ie.predTaken)) {
      val bEntry = rob(ie.robIdx)
      ratG := bEntry.cpRatG
      ratF := bEntry.cpRatF
      // Note: free/busy vectors must NOT be restored from the checkpoint.
      // Older instructions may have issued/committed since the branch was
      // dispatched, and restoring busy bits would deadlock on registers that
      // are already written.  Instead undo the allocations of flushed entries.
      for (k <- 0 until RobSize) {
        when(rob(k).valid && rob(k).seq > bEntry.seq) {
          when(rob(k).hasDst) { freeG(rob(k).pdst) := true.B }
          when(rob(k).hasDstF) { freeF(rob(k).pdstF) := true.B }
          rob(k).valid := false.B
        }
      }
      for (k <- 0 until IqSize) {
        when(iq(k).valid && iq(k).seq > bEntry.seq) { iq(k).valid := false.B }
      }
      tailSeq := bEntry.seq + 1.U
      pc := Mux(issueTaken, issueTarget, ie.uop.pc + ie.uop.len)
      ibufValid := false.B
      fetchStop := false.B
      fState := 0.U
      // drop younger memory queue / store buffer entries
      for (k <- 0 until 8) {
        when(mq(k).valid && mq(k).seq > bEntry.seq) { mq(k).valid := false.B }
        when(sb(k).valid && sb(k).seq > bEntry.seq) { sb(k).valid := false.B }
      }
      when(ldWait && ldSeq > bEntry.seq) { ldWait := false.B }
      when(stWait && stSeq > bEntry.seq) { stWait := false.B }
      val keepCnt = (0 until 8).map { k =>
        val e = mq((mqHead + k.U)(2, 0))
        (e.valid && e.seq <= bEntry.seq).asUInt
      }.reduce(_ +& _)
      mqTail := mqHead + keepCnt
      mqCount := keepCnt
    }
  }


  // --------------------------------------------------------------- dispatch
  private val robUsed = tailSeq - headSeq
  private val robSpace = robUsed < RobSize.U
  private val iqUsed = iq.map(_.valid.asUInt).reduce(_ +& _)
  private val iqSpace = iqUsed < IqSize.U

  private val decValid = canUse && !dec.io.illegal && !fetchStop && !haltedReg && !fatalReg
  private val dUop = d

  private val isMemUop = (dUop.a.kind === OperandKind.Mem.U || dUop.b.kind === OperandKind.Mem.U) &&
    (dUop.op === Ops.MOV.U || dUop.op === Ops.MOVZX.U ||
      dUop.op === Ops.MOVSX.U || dUop.op === Ops.MOVSXD.U)
  private val memIsStore = dUop.a.kind === OperandKind.Mem.U
  private val memIsLoad = dUop.b.kind === OperandKind.Mem.U
  private val mqSpace = mqCount < 8.U
  private val isSse = dUop.op >= Ops.MOVD.U && dUop.op <= Ops.CVTTPD2DQ.U
  private val isMulDiv = dUop.op === Ops.MUL.U || dUop.op === Ops.IMUL.U ||
    dUop.op === Ops.DIV.U || dUop.op === Ops.IDIV.U
  private val isSys = dUop.op === Ops.RDMSR.U || dUop.op === Ops.WRMSR.U ||
    dUop.op === Ops.SYSCALL.U || dUop.op === Ops.SYSRET.U || dUop.op === Ops.IRET.U ||
    dUop.op === Ops.CPUID.U || dUop.op === Ops.MOV_CR.U || dUop.op === Ops.INT.U ||
    dUop.op === Ops.INT3.U || dUop.op === Ops.UD2.U || dUop.op === Ops.LOOP.U ||
    dUop.op === Ops.MOVS.U || dUop.op === Ops.STOS.U || dUop.op === Ops.LODS.U ||
    dUop.op === Ops.SCAS.U || dUop.op === Ops.CMPS.U

  private val dIsPush = dUop.op === Ops.PUSH.U
  private val dIsPop  = dUop.op === Ops.POP.U
  private val dIsCall = dUop.op === Ops.CALL.U && dUop.b.kind === OperandKind.Imm.U
  private val dIsCallReg = dUop.op === Ops.CALL.U && dUop.a.kind === OperandKind.Gpr.U
  private val dIsJmpReg  = dUop.op === Ops.JMP.U && dUop.a.kind === OperandKind.Gpr.U
  private val dIsJmpMem  = dUop.op === Ops.JMP.U && dUop.a.kind === OperandKind.Mem.U
  private val dSseComisMem = dUop.b.kind === OperandKind.Mem.U &&
    (dUop.op === Ops.SIMD_COMI.U || dUop.op === Ops.SIMD_UCOMI.U)
  private val dAluRmw = dUop.a.kind === OperandKind.Mem.U &&
    (dUop.op === Ops.ADD.U || dUop.op === Ops.SUB.U || dUop.op === Ops.AND.U ||
      dUop.op === Ops.OR.U || dUop.op === Ops.XOR.U || dUop.op === Ops.ADC.U ||
      dUop.op === Ops.SBB.U || dUop.op === Ops.INC.U || dUop.op === Ops.DEC.U ||
      dUop.op === Ops.NEG.U || dUop.op === Ops.NOT.U || dUop.op === Ops.SHL.U ||
      dUop.op === Ops.SHR.U || dUop.op === Ops.SAR.U || dUop.op === Ops.ROL.U ||
      dUop.op === Ops.ROR.U || dUop.op === Ops.RCL.U || dUop.op === Ops.RCR.U ||
      dUop.op === Ops.XCHG.U || dUop.op === Ops.XADD.U)
  private val dAluMemSrc = dUop.b.kind === OperandKind.Mem.U &&
    (dUop.op === Ops.ADD.U || dUop.op === Ops.SUB.U || dUop.op === Ops.AND.U ||
      dUop.op === Ops.OR.U || dUop.op === Ops.XOR.U || dUop.op === Ops.CMP.U ||
      dUop.op === Ops.TEST.U || dUop.op === Ops.ADC.U || dUop.op === Ops.SBB.U)
  private val dIsRet  = dUop.op === Ops.RET.U && dUop.b.kind === OperandKind.None.U
  private val dIsPushF = dUop.op === Ops.PUSHF.U
  private val dIsPopF  = dUop.op === Ops.POPF.U
  private val dIsStack = dIsPush || dIsPop || dIsCall || dIsCallReg || dIsRet || dIsPushF || dIsPopF

  private val dIsLoopCc = dUop.op === Ops.LOOPNE.U || dUop.op === Ops.LOOPE.U
  private val dIsJrcxz = dUop.op === Ops.JRCXZ.U
  private val dIsBranch = dUop.op === Ops.JCC.U || dUop.op === Ops.JMP.U || dIsCall ||
    dIsCallReg || dIsJmpReg || dIsJmpMem || dIsRet || dIsLoopCc || dIsJrcxz
  private val dIsSse = isSseOp(dUop.op)
  private val dSseMem = dIsSse && (dUop.a.kind === OperandKind.Mem.U || dUop.b.kind === OperandKind.Mem.U)
  private val dSseMemMove = dSseMem && isSseMoveOp(dUop.op)
  private val dSseArithOp = dUop.op === Ops.SIMD_ADD.U || dUop.op === Ops.SIMD_SUB.U ||
    dUop.op === Ops.SIMD_MUL.U || dUop.op === Ops.SIMD_DIV.U ||
    dUop.op === Ops.SIMD_SQRT.U || dUop.op === Ops.SIMD_MIN.U ||
    dUop.op === Ops.SIMD_MAX.U
  private val dSseMemArith = dSseMem && dSseArithOp && !dSseMemMove &&
    dUop.b.kind === OperandKind.Mem.U
  private val dSseMemLoad = dSseMemMove && dUop.b.kind === OperandKind.Mem.U
  private val dSseMemStore = dSseMemMove && dUop.a.kind === OperandKind.Mem.U
  private val dMemUop = isMemUop || dSseMemMove || dSseMemArith || dIsJmpMem ||
    dSseComisMem || dAluMemSrc || dAluRmw
  private val dSseMemSize = Mux(dUop.op === Ops.MOVDQ.U, 16.U,
    Mux(dUop.op === Ops.MOVQ.U, 8.U,
      Mux(dUop.op === Ops.MOVD.U, dUop.opsz,
        Mux(dUop.op === Ops.MOVPS.U,
          Mux(dUop.simd === SimdKind.SS.U, 4.U,
            Mux(dUop.simd === SimdKind.SD.U, 8.U, 16.U)),
          dUop.memsz))))
  private val dSseDstX = dIsSse && dUop.a.kind === OperandKind.Xmm.U && !isComisOp(dUop.op)
  private val dSseDstG = dIsSse && dUop.a.kind === OperandKind.Gpr.U
  private val dSseFlags = dIsSse && isComisOp(dUop.op)
  private val dSseNeedXA = dIsSse && dUop.a.kind === OperandKind.Xmm.U
  private val dSseNeedXB = dIsSse && dUop.b.kind === OperandKind.Xmm.U
  private val dSseNeedB  = dIsSse && dUop.b.kind === OperandKind.Gpr.U
  private val dIsMulDiv = dUop.op === Ops.MUL.U || dUop.op === Ops.IMUL.U ||
    dUop.op === Ops.DIV.U || dUop.op === Ops.IDIV.U
  private val dIsMulDivOne = dIsMulDiv && dUop.a.kind === OperandKind.Gpr.U &&
    !(dUop.op === Ops.IMUL.U && dUop.imm =/= 0.U)
  private val dIsCdq = dUop.op === Ops.CDQ.U
  private val dIsCbw = dUop.op === Ops.CBW.U
  private val dIsMovCr = dUop.op === Ops.MOV_CR.U
  private val dMovCrWrite = dIsMovCr && dUop.cond(0)
  private val dMovCrRead = dIsMovCr && !dUop.cond(0)
  private val dWritesG = writesGprOp(dUop) || dSseDstG || dUop.op === Ops.POP.U ||
    dMovCrRead || dIsMulDivOne || dIsCbw || dIsLoopCc ||
    ((dUop.op === Ops.XCHG.U || dUop.op === Ops.XADD.U) &&
      dUop.a.kind === OperandKind.Mem.U && dUop.b.kind === OperandKind.Gpr.U)
  private val dWritesF = writesFlagsOp(dUop.op) || dSseFlags || dIsPopF || dIsMulDivOne
  private val dNeedsF = needsFlagsOp(dUop.op) || dSseFlags || dIsPushF || dIsLoopCc
  private val dHasMemAddr = dUop.mem.hasBase || dUop.mem.hasIndex

  private val gFreeIdx = PriorityEncoder(freeG.asUInt)
  private val fFreeIdx = PriorityEncoder(freeF.asUInt)
  private val xFreeIdx = PriorityEncoder(freeX.asUInt)
  private val gFreeOk = freeG.asUInt.orR
  private val fFreeOk = freeF.asUInt.orR
  private val xFreeOk = freeX.asUInt.orR
  private val needG = dWritesG
  private val needF = dWritesF
  private val needX = dSseDstX || dSseMemLoad || dSseMemArith
  private val needG2 = dIsMulDivOne || dIsCdq
  private val sseDstG = dSseDstG

  private val freeG2 = Wire(Vec(64, Bool()))
  freeG2 := freeG
  when(needG) { freeG2(gFreeIdx) := false.B }
  private val gFreeIdx2 = PriorityEncoder(freeG2.asUInt)
  private val gFreeOk2 = freeG2.asUInt.orR

  private val canDispatch = decValid && robSpace && !issueMispredict && !mqFlush &&
    Mux(dMemUop || dIsStack, mqSpace, iqSpace) &&
    (!needG || gFreeOk) && (!needF || fFreeOk) && (!needX || xFreeOk) &&
    (!dIsStack || gFreeOk2) && (!needG2 || (gFreeOk && gFreeOk2)) &&
    (isAluOp(dUop.op) || dIsSse || dIsStack || dIsMovCr || dIsMulDivOne ||
      dIsCdq || dIsJmpMem || dAluRmw) &&
    (!dSseMem || dSseMemMove || dSseMemArith || dSseComisMem)

  private val dispatchOk = canDispatch

  private val predTaken = (dUop.op === Ops.JMP.U && dUop.b.kind === OperandKind.Imm.U) ||
    dIsCall || dIsLoopCc || dIsJrcxz
  private val predTarget = Mux(dIsLoopCc || dIsJrcxz || dUop.b.kind === OperandKind.Imm.U,
    dUop.pc + dUop.len + dUop.imm, dUop.pc + dUop.len)
  private val nextPc = Mux(dIsBranch, Mux(predTaken, predTarget, dUop.pc + dUop.len),
    dUop.pc + dUop.len)

  private val srcA = Mux(dUop.a.kind === OperandKind.Gpr.U || dMovCrWrite, ratG(dUop.a.idx), 0.U(6.W))
  private val srcB = Mux(dUop.b.kind === OperandKind.Gpr.U, ratG(dUop.b.idx), 0.U(6.W))
  private val srcBase = Mux(dUop.mem.hasBase, ratG(dUop.mem.base), 0.U(6.W))
  private val srcIdx = Mux(dUop.mem.hasIndex, ratG(dUop.mem.index), 0.U(6.W))
  private val srcF = ratF(0)

  private val newPdst = Mux(needG, gFreeIdx, 0.U)
  private val newPdstF = Mux(needF, fFreeIdx, 0.U)
  private val newPdstX = Mux(needX, xFreeIdx, 0.U)
  private val oldPdstX = Mux(dUop.a.kind === OperandKind.Xmm.U, ratX(dUop.a.idx), 0.U)
  private val stackOldRsp = ratG(4)
  private val stackNewRsp = Mux(needG, gFreeIdx2, gFreeIdx)
  private val dstArchIdx = Mux((dUop.op === Ops.XCHG.U || dUop.op === Ops.XADD.U) &&
    dUop.a.kind === OperandKind.Mem.U, dUop.b.idx, dUop.a.idx)
  private val oldPdst = Mux(dIsMulDivOne || dIsCbw, ratG(0),
    Mux(dUop.a.kind === OperandKind.Gpr.U ||
      ((dUop.op === Ops.XCHG.U || dUop.op === Ops.XADD.U) &&
        dUop.b.kind === OperandKind.Gpr.U),
      ratG(dstArchIdx), 0.U))
  private val oldPdstF = ratF(0)

  private val iqFreeVec = Wire(Vec(IqSize, Bool()))
  for (i <- 0 until IqSize) {
    iqFreeVec(i) := !iq(i).valid && !(issueValid && issueIdx === i.U) &&
      !(issueValid1 && issueIdx1 === i.U)
  }
  private val iqSlot = PriorityEncoder(iqFreeVec.asUInt)
  private val robIdx = tailSeq(4, 0)

  when(dispatchOk) {
    // ROB entry with checkpoint
    rob(robIdx).valid := true.B
    rob(robIdx).seq := tailSeq
    rob(robIdx).uop := dUop
    rob(robIdx).hasDst := needG
    rob(robIdx).pdst := newPdst
    rob(robIdx).oldDst := oldPdst
    rob(robIdx).hasDstF := needF
    rob(robIdx).pdstF := newPdstF
    rob(robIdx).oldDstF := oldPdstF
    rob(robIdx).hasDstX := needX
    rob(robIdx).pdstX := newPdstX
    rob(robIdx).oldDstX := oldPdstX
    rob(robIdx).hasDst2 := dIsStack || dIsMulDivOne || dIsCdq
    rob(robIdx).pdst2 := Mux(dIsCdq, gFreeIdx, Mux(dIsMulDivOne, gFreeIdx2, stackNewRsp))
    rob(robIdx).oldDst2 := Mux(dIsCdq || dIsMulDivOne, ratG(2), stackOldRsp)
    rob(robIdx).done := false.B
    rob(robIdx).value := 0.U
    rob(robIdx).flagsVal := 0.U
    rob(robIdx).taken := false.B
    rob(robIdx).target := 0.U
    rob(robIdx).isBranch := dIsBranch
    rob(robIdx).predTaken := predTaken
    rob(robIdx).isStore := (isMemUop && memIsStore) || dIsPush || dIsCall ||
      dIsCallReg || dIsPushF || dSseMemStore || dAluRmw
    rob(robIdx).memAddr := 0.U
    rob(robIdx).memData := 0.U
    rob(robIdx).memSize := dUop.memsz
    rob(robIdx).storeDone := false.B
    rob(robIdx).cpRatG := ratG
    rob(robIdx).cpRatF := ratF
    rob(robIdx).cpRatX := ratX
    rob(robIdx).cpFreeG := freeG
    rob(robIdx).cpFreeF := freeF
    rob(robIdx).cpBusyG := busyG
    rob(robIdx).cpBusyF := busyF

    // IQ entry (register operations only)
    when(!dMemUop && !dIsStack) {
    iq(iqSlot).valid := true.B
    iq(iqSlot).seq := tailSeq
    iq(iqSlot).uop := dUop
    iq(iqSlot).robIdx := robIdx
    iq(iqSlot).psrcA := Mux(dIsMulDivOne || dIsCdq || dIsCbw, ratG(0), srcA)
    iq(iqSlot).psrcB := Mux(dIsMulDivOne, ratG(dUop.a.idx), srcB)
    iq(iqSlot).psrcF := srcF
    iq(iqSlot).psrcXA := Mux(dUop.a.kind === OperandKind.Xmm.U, ratX(dUop.a.idx), 0.U)
    iq(iqSlot).psrcXB := Mux(dUop.b.kind === OperandKind.Xmm.U, ratX(dUop.b.idx), 0.U)
    iq(iqSlot).psrcBase := srcBase
    iq(iqSlot).psrcIdx := srcIdx
    iq(iqSlot).needA := (dUop.a.kind === OperandKind.Gpr.U && !dSseDstG) ||
      dMovCrWrite || dIsCdq || dIsCbw
    iq(iqSlot).needB := (dUop.b.kind === OperandKind.Gpr.U) || dIsMulDivOne
    iq(iqSlot).needF := dNeedsF
    iq(iqSlot).needXA := dSseNeedXA
    iq(iqSlot).needXB := dSseNeedXB
    iq(iqSlot).psrcC := ratG(2)
    iq(iqSlot).needC := dIsMulDivOne
    iq(iqSlot).needBase := dUop.mem.hasBase && dUop.op === Ops.LEA.U
    iq(iqSlot).needIdx := dUop.mem.hasIndex && dUop.op === Ops.LEA.U
    iq(iqSlot).hasDst := needG
    iq(iqSlot).pdst := newPdst
    iq(iqSlot).hasDstF := needF
    iq(iqSlot).pdstF := newPdstF
    iq(iqSlot).hasDstX := needX
    iq(iqSlot).pdstX := newPdstX
    iq(iqSlot).hasDst2 := dIsMulDivOne || dIsCdq
    iq(iqSlot).pdst2 := Mux(dIsCdq, gFreeIdx, gFreeIdx2)
    iq(iqSlot).isBranch := dIsBranch
    iq(iqSlot).predTaken := predTaken
    iq(iqSlot).target := predTarget
    }

    // memory queue entry
    when(dMemUop || dIsStack) {
      mq(mqTail).isPush := dIsPush
      mq(mqTail).isPop := dIsPop
      mq(mqTail).isCall := dIsCall
      mq(mqTail).isCallReg := dIsCallReg
      mq(mqTail).psrcTarget := Mux(dIsCallReg, ratG(dUop.a.idx), 0.U)
      mq(mqTail).needTarget := dIsCallReg
      mq(mqTail).isRet := dIsRet
      mq(mqTail).isJmpMem := dIsJmpMem
      mq(mqTail).isComisMem := dSseComisMem
      mq(mqTail).isAluMem := dAluMemSrc
      mq(mqTail).isAluRmw := dAluRmw
      mq(mqTail).psrcB := Mux(dAluRmw && dUop.b.kind === OperandKind.Gpr.U,
        ratG(dUop.b.idx), 0.U)
      mq(mqTail).srcBImm := dUop.imm
      mq(mqTail).psrcA := Mux(dAluMemSrc, ratG(dUop.a.idx), 0.U)
      mq(mqTail).needA := dAluMemSrc
      mq(mqTail).isPushF := dIsPushF
      mq(mqTail).isPopF := dIsPopF
      mq(mqTail).hasDstF := dIsPopF || dSseComisMem ||
        (dAluRmw && dUop.op =/= Ops.XCHG.U) || (dAluMemSrc && dWritesF)
      mq(mqTail).pdstF := newPdstF
      mq(mqTail).psrcF := srcF
      mq(mqTail).isSseMem := dSseMemMove
      mq(mqTail).isSseArith := dSseMemArith
      mq(mqTail).sseStore := dSseMemStore
      mq(mqTail).hasDstX := dSseMemLoad || dSseMemArith
      mq(mqTail).pdstX := newPdstX
      mq(mqTail).oldDstX := oldPdstX
      mq(mqTail).psrcXB := Mux(dUop.b.kind === OperandKind.Xmm.U, ratX(dUop.b.idx), 0.U)
      mq(mqTail).needXB := dUop.b.kind === OperandKind.Xmm.U
      mq(mqTail).psrcRsp := stackOldRsp
      mq(mqTail).pdst2 := stackNewRsp
      mq(mqTail).hasDst2 := dIsStack
      mq(mqTail).valid := true.B
      mq(mqTail).seq := tailSeq
      mq(mqTail).uop := dUop
      mq(mqTail).robIdx := robIdx
      mq(mqTail).psrcBase := srcBase
      mq(mqTail).psrcIdx := srcIdx
      mq(mqTail).psrcData := Mux(dIsPush && dUop.a.kind === OperandKind.Gpr.U, ratG(dUop.a.idx), srcB)
      mq(mqTail).needBase := dUop.mem.hasBase
      mq(mqTail).needIdx := dUop.mem.hasIndex
      mq(mqTail).needData := (memIsStore && dUop.b.kind === OperandKind.Gpr.U) ||
        (dIsPush && dUop.a.kind === OperandKind.Gpr.U)
      mq(mqTail).hasDst := needG && !dSseMemLoad
      mq(mqTail).pdst := newPdst
      mq(mqTail).oldDst := oldPdst
      mq(mqTail).isStore := memIsStore && !dIsStack && !dIsJmpMem && !dAluRmw
      mq(mqTail).memSize := Mux(dAluRmw, dUop.opsz, Mux(dSseComisMem, 8.U, Mux(dIsJmpMem, 8.U, Mux(dIsCallReg, 8.U, Mux(dSseMemArith, 16.U,
        Mux(dSseMemMove, dSseMemSize,
          Mux(dIsCall || dIsRet || dIsPushF || dIsPopF, 8.U, dUop.memsz)))))))
      mqTail := mqTail + 1.U
      mqPush := true.B
    }

    // rename
    when(needG && !dIsMulDivOne) {
      ratG(dstArchIdx) := newPdst
      freeG(gFreeIdx) := false.B
      busyG(newPdst) := true.B
    }
    when(needF) {
      ratF(0) := newPdstF
      freeF(fFreeIdx) := false.B
      busyF(newPdstF) := true.B
    }
    when(needX) {
      ratX(dUop.a.idx) := newPdstX
      freeX(xFreeIdx) := false.B
      busyX(newPdstX) := true.B
    }
    when(sseDstG) {
      ratG(dUop.a.idx) := newPdst
      freeG(gFreeIdx) := false.B
      busyG(newPdst) := true.B
    }
    when(dIsCdq) {
      ratG(2) := gFreeIdx
      freeG(gFreeIdx) := false.B
      busyG(gFreeIdx) := true.B
    }
    when(dIsCbw) {
      ratG(0) := newPdst
      freeG(gFreeIdx) := false.B
      busyG(newPdst) := true.B
    }
    when(dIsMulDivOne) {
      ratG(0) := newPdst
      ratG(2) := gFreeIdx2
      freeG(gFreeIdx) := false.B
      freeG(gFreeIdx2) := false.B
      busyG(newPdst) := true.B
      busyG(gFreeIdx2) := true.B
    }
    when(dIsStack) {
      when(needG) {
        ratG(dUop.a.idx) := newPdst
        freeG(gFreeIdx) := false.B
        busyG(newPdst) := true.B
      }
      ratG(4) := stackNewRsp
      freeG(stackNewRsp) := false.B
      busyG(stackNewRsp) := true.B
    }
    tailSeq := tailSeq + 1.U
    pc := nextPc
    when(dUop.op === Ops.HLT.U) { fetchStop := true.B }
  }

  // illegal/unsupported decode: fail fast instead of stalling forever
  when(decValid && (dec.io.illegal ||
    (!isAluOp(dUop.op) && !dIsSse && !dIsStack && !dIsMovCr && !dIsMulDivOne &&
      !dIsCdq && !dIsCbw && !dIsJmpMem && !dAluRmw) ||
    (dSseMem && !dSseMemMove && !dSseMemArith && !dSseComisMem))) {
    haltedReg := true.B
    fatalReg := true.B
    fatalPcReg := pc
    fatalCdReg := ExceptionCause.Ud.U
  }



  // ------------------------------------------------------- second ALU port
  private val rsA1 = prfG(ie1.psrcA)
  private val rsB1 = prfG(ie1.psrcB)
  private val rsF1 = prfF(ie1.psrcF)

  private val srcBits1 = ie1.uop.memsz << 3
  private val movExt1 = lut(ie1.uop.op, rsB1,
    Seq(
      Ops.MOVZX -> (rsB1 & ((1.U(64.W) << srcBits1) - 1.U)(63, 0)),
      Ops.MOVSX -> ({
        val m = ((1.U(64.W) << srcBits1) - 1.U)(63, 0)
        val sign = (rsB1 >> (srcBits1 - 1.U))(0)
        (rsB1 & m) | Mux(sign, ~m, 0.U(64.W))
      }),
      Ops.MOVSXD -> ((rsB1 & "hffffffff".U) |
        Mux(rsB1(31), "hffffffff00000000".U, 0.U(64.W)))
    ))
  private val aluB1 = Mux(ie1.uop.b.kind === OperandKind.Imm.U, ie1.uop.imm,
    Mux(ie1.uop.op === Ops.MOVZX.U || ie1.uop.op === Ops.MOVSX.U ||
      ie1.uop.op === Ops.MOVSXD.U, movExt1, rsB1))

  private val ea1 = WireDefault(0.U(64.W))
  when(ie1.uop.op === Ops.LEA.U) {
    val baseVal1 = Mux(ie1.uop.mem.ripRel, ie1.uop.pc + ie1.uop.len,
      Mux(ie1.uop.mem.hasBase, prfG(ie1.psrcBase), 0.U))
    ea1 := baseVal1 + Mux(ie1.uop.mem.hasIndex,
      prfG(ie1.psrcIdx) << ie1.uop.mem.scale, 0.U) + ie1.uop.mem.disp
  }

  val alu1 = Module(new IntAlu)
  alu1.io.op := ie1.uop.op
  alu1.io.opsz := ie1.uop.opsz
  alu1.io.a := rsA1
  alu1.io.b := aluB1
  alu1.io.hiIn := 0.U
  alu1.io.imm := ie1.uop.imm
  alu1.io.addrIn := ea1
  alu1.io.flagsIn := rsF1
  alu1.io.cond := ie1.uop.cond
  alu1.io.oneOp := false.B

  val sse1 = Module(new SseAlu)
  sse1.io.op := ie1.uop.op
  sse1.io.simd := ie1.uop.simd
  sse1.io.opsz := ie1.uop.opsz
  sse1.io.a := Mux(ie1.needXA, prfX(ie1.psrcXA),
    Mux(ie1.uop.a.kind === OperandKind.Gpr.U, prfG(ie1.psrcA).pad(128), 0.U(128.W)))
  sse1.io.b := Mux(ie1.needXB, prfX(ie1.psrcXB),
    Mux(ie1.uop.b.kind === OperandKind.Gpr.U,
      Mux(ie1.uop.opsz === 4.U, prfG(ie1.psrcB)(31, 0).pad(128), prfG(ie1.psrcB).pad(128)),
      0.U(128.W)))
  sse1.io.imm := ie1.uop.imm(7, 0)
  sse1.io.flagsIn := 0.U

  private val wbVal1 = Mux(ie1.uop.opsz === 1.U,
    (rsA1 & "hffffffffffffff00".U(64.W)) | (alu1.io.result & "hff".U),
    Mux(ie1.uop.opsz === 2.U,
      (rsA1 & "hffffffffffff0000".U(64.W)) | (alu1.io.result & "hffff".U),
      Mux(ie1.uop.opsz === 4.U, alu1.io.result & "hffffffff".U(64.W), alu1.io.result)))
  private val sseGprVal1 = Mux(ie1.uop.opsz === 1.U,
    (rsA1 & "hffffffffffffff00".U) | (sse1.io.gprResult & "hff".U),
    Mux(ie1.uop.opsz === 2.U,
      (rsA1 & "hffffffffffff0000".U) | (sse1.io.gprResult & "hffff".U),
      Mux(ie1.uop.opsz === 4.U, sse1.io.gprResult & "hffffffff".U(64.W), sse1.io.gprResult)))

  when(issueValid1) {
    iq(issueIdx1).valid := false.B
    when(ie1.hasDst) {
      prfG(ie1.pdst) := Mux(isSseOp(ie1.uop.op), sseGprVal1, wbVal1)
      busyG(ie1.pdst) := false.B
    }
    when(ie1.hasDstX) {
      prfX(ie1.pdstX) := sse1.io.result
      busyX(ie1.pdstX) := false.B
    }
    when(ie1.hasDstF) {
      prfF(ie1.pdstF) := Mux(isSseOp(ie1.uop.op), sse1.io.flags, alu1.io.flags)
      busyF(ie1.pdstF) := false.B
    }
    rob(ie1.robIdx).done := true.B
    rob(ie1.robIdx).value := Mux(isSseOp(ie1.uop.op), sseGprVal1, wbVal1)
    issue2Cnt := issue2Cnt + 1.U
  }

  // ------------------------------------------------------- memory execution
  private val me = mq(mqHead)
  private val meAddrReady = (!me.needBase || !busyG(me.psrcBase)) &&
    (!me.needIdx || !busyG(me.psrcIdx))
  private val meDataReady = !me.needData || !busyG(me.psrcData)
  private val meRspReady = !(me.isPush || me.isPop || me.isCall || me.isCallReg ||
    me.isRet || me.isPushF || me.isPopF) || !busyG(me.psrcRsp)
  private val meTgtReady = !me.needTarget || !busyG(me.psrcTarget)
  private val meAReady = !me.needA || !busyG(me.psrcA)
  private val meBReady = !(me.isAluRmw && me.uop.b.kind === OperandKind.Gpr.U) ||
    !busyG(me.psrcB)
  private val meFReady = !(me.isPushF || me.isPopF) || !busyF(me.psrcF)
  private val meOperandsReady = meAddrReady && meDataReady && meRspReady && meFReady
  private val meOperandsReady2 = meOperandsReady && meTgtReady && meAReady && meBReady
  private val meReady = me.valid && meOperandsReady2 && (!pgOn || xlDone)
  private val meBaseVal = Mux(me.uop.mem.ripRel, me.uop.pc + me.uop.len,
    Mux(me.needBase, prfG(me.psrcBase), 0.U(64.W)))
  private val meAddr = meBaseVal +
    Mux(me.needIdx, prfG(me.psrcIdx) << me.uop.mem.scale, 0.U(64.W)) + me.uop.mem.disp
  private val meData = Mux(me.uop.b.kind === OperandKind.Imm.U, me.uop.imm,
    prfG(me.psrcData))
  private def sseSizeOf(u: Uop): UInt = Mux(u.op === Ops.MOVDQ.U, 16.U,
    Mux(u.op === Ops.MOVQ.U, 8.U,
      Mux(u.op === Ops.MOVD.U, u.opsz,
        Mux(u.op === Ops.MOVPS.U,
          Mux(u.simd === SimdKind.SS.U, 4.U,
            Mux(u.simd === SimdKind.SD.U, 8.U, 16.U)),
          u.memsz))))
  private val meData128 = Mux(me.needXB, prfX(me.psrcXB), meData.pad(128))
  private val popfMask = "hed5".U(64.W) // CF PF AF ZF SF IF DF OF
  private def popfFlags(raw: UInt): UInt =
    (prfF(me.psrcF) & ~popfMask) | (raw & popfMask) | "h2".U

  private val loadData64 = io.dmem.respData(63, 0)
  private def sseLoadValue(raw: UInt): UInt = Mux(sseSizeOf(me.uop) === 16.U, raw,
    Mux(sseSizeOf(me.uop) === 8.U, raw(63, 0).pad(128), raw(31, 0).pad(128)))

  private val sbFreeOk = sb.map(e => !e.valid).reduce(_ || _)
  private val sbFreeIdx = PriorityEncoder(VecInit(sb.map(e => !e.valid)).asUInt)
  private val meVaddr = Mux(me.isPop || me.isRet || me.isPopF, prfG(me.psrcRsp),
    Mux(me.isPush || me.isPushF || me.isCall || me.isCallReg,
      prfG(me.psrcRsp) - me.memSize, meAddr))

  val mmu = Module(new Mmu)
  mmu.io.vaddr   := meVaddr
  mmu.io.isWrite := me.isStore || me.isPush || me.isPushF || me.isCall || me.isCallReg
  mmu.io.isFetch := false.B
  mmu.io.pg      := pgOn
  mmu.io.wp      := cr0(16)
  mmu.io.nxe     := false.B
  mmu.io.cr3     := cr3
  mmu.io.flush   := cr3Flush
  mmu.io.ptwData := io.ptw.respData
  mmu.io.ptwValid := io.ptw.respValid

  private val needXlat = me.valid && pgOn && meOperandsReady2 && !xlWait && !xlDone
  mmu.io.reqValid := needXlat
  // Register the PTW request so the walk address is stable when the
  // testbench (or an SRAM-backed PTW port) samples it. The MMU simply waits
  // for ptwValid, so one cycle of extra latency is harmless.
  private val ptwReqReg  = RegNext(mmu.io.ptwReq)
  private val ptwAddrReg = RegNext(mmu.io.ptwAddr)
  io.ptw.reqValid := ptwReqReg
  io.ptw.reqAddr  := ptwAddrReg
  when(needXlat) { xlWait := true.B }
  when(mmu.io.done && xlWait) {
    xlWait := false.B
    when(mmu.io.fault || mmu.io.isGp) {
      haltedReg := true.B
      fatalReg := true.B
      fatalPcReg := me.uop.pc
      fatalCdReg := ExceptionCause.Pf.U
    }.otherwise {
      xlPaddr := mmu.io.paddr
      xlDone := true.B
    }
  }

  private val mePaddr = Mux(pgOn, xlPaddr, meVaddr)
  private val sbHit = sb.map(e => e.valid && e.addr === mePaddr && e.size === me.memSize)
  private val sbSel = Wire(Vec(8, Bool()))
  for (i <- 0 until 8) {
    var newest = true.B
    for (j <- 0 until 8) {
      if (i != j) {
        newest = newest && (!(sbHit(j) && sb(j).seq > sb(i).seq))
      }
    }
    sbSel(i) := sbHit(i) && newest
  }
  private val sbHitAny = sbSel.reduce(_ || _)
  private val sbHitData = PriorityMux(sbSel.zip(sb.map(_.data)))
  private val sbHitData64 = sbHitData(63, 0)

  // page-table walks must see not-yet-committed stores from the store buffer
  private val ptwHit = sb.map(e => e.valid && e.addr === mmu.io.ptwAddr && e.size === 8.U)
  private val ptwSel = Wire(Vec(8, Bool()))
  for (i <- 0 until 8) {
    var newest = true.B
    for (j <- 0 until 8) {
      if (i != j) newest = newest && (!(ptwHit(j) && sb(j).seq > sb(i).seq))
    }
    ptwSel(i) := ptwHit(i) && newest
  }
  when(ptwSel.reduce(_ || _)) {
    mmu.io.ptwData := PriorityMux(ptwSel.zip(sb.map(_.data)))(63, 0)
  }

  val sseMem = Module(new SseAlu)
  sseMem.io.op := me.uop.op
  sseMem.io.simd := me.uop.simd
  sseMem.io.opsz := me.uop.opsz
  sseMem.io.a := prfX(me.oldDstX)
  sseMem.io.b := Mux(sbHitAny, sbHitData, io.dmem.respData)
  sseMem.io.imm := me.uop.imm(7, 0)
  sseMem.io.flagsIn := prfF(me.psrcF)

  val aluRmwM = Module(new IntAlu)
  aluRmwM.io.op      := Mux(me.uop.op === Ops.XADD.U, Ops.ADD.U, me.uop.op)
  aluRmwM.io.opsz    := me.uop.opsz
  aluRmwM.io.a       := Mux(sbHitAny, sbHitData64, io.dmem.respData(63, 0))
  aluRmwM.io.b       := Mux(me.uop.b.kind === OperandKind.Imm.U, me.srcBImm,
    prfG(me.psrcB))
  aluRmwM.io.hiIn    := 0.U
  aluRmwM.io.imm     := me.uop.imm
  aluRmwM.io.addrIn  := 0.U
  aluRmwM.io.flagsIn := prfF(me.psrcF)
  aluRmwM.io.cond    := me.uop.cond
  aluRmwM.io.oneOp   := false.B

  val aluM = Module(new IntAlu)
  aluM.io.op      := me.uop.op
  aluM.io.opsz    := me.uop.opsz
  aluM.io.a       := prfG(me.psrcA)
  aluM.io.b       := Mux(sbHitAny, sbHitData64, io.dmem.respData(63, 0))
  aluM.io.hiIn    := 0.U
  aluM.io.imm     := me.uop.imm
  aluM.io.addrIn  := 0.U
  aluM.io.flagsIn := prfF(me.psrcF)
  aluM.io.cond    := me.uop.cond
  aluM.io.oneOp   := false.B

  private def aluMerge(res: UInt): UInt = Mux(me.uop.opsz === 8.U, res,
    Mux(me.uop.opsz === 2.U,
      (prfG(me.oldDst) & "hffffffffffff0000".U(64.W)) | (res & "hffff".U),
      (prfG(me.oldDst) & "hffffffffffffff00".U(64.W)) | (res & "hff".U)))

  private def zextLoad(u: Uop, raw: UInt): UInt = lut(u.memsz, raw,
    Seq(1.U -> (raw & "hff".U), 2.U -> (raw & "hffff".U),
      4.U -> (raw & "hffffffff".U), 8.U -> raw))
  private def sextLoad(u: Uop, raw: UInt): UInt = lut(u.memsz, raw,
    Seq(1.U -> Cat(Fill(56, raw(7)), raw(7, 0)),
      2.U -> Cat(Fill(48, raw(15)), raw(15, 0)),
      4.U -> Cat(Fill(32, raw(31)), raw(31, 0)),
      8.U -> raw))

  private def mergedLoad(u: Uop, raw: UInt, old: UInt): UInt = Mux(u.opsz === 8.U, raw,
    Mux(u.opsz === 1.U, (old & "hffffffffffffff00".U) | (raw & "hff".U),
      Mux(u.opsz === 2.U, (old & "hffffffffffff0000".U) | (raw & "hffff".U),
        raw & "hffffffff".U)))

  private val loadValue = Mux(me.uop.op === Ops.MOVZX.U, zextLoad(me.uop, loadData64),
    Mux(me.uop.op === Ops.MOVSX.U || me.uop.op === Ops.MOVSXD.U,
      sextLoad(me.uop, loadData64),
      mergedLoad(me.uop, loadData64, prfG(me.oldDst))))

  private val headEntry = rob(headSeq(4, 0))
  private val commitStoreReq = headEntry.valid && headEntry.done && headEntry.isStore &&
    !headEntry.storeDone && !stWait
  private val loadReq = meReady && !me.isStore && !me.isPush && !me.isPop && !me.isRet &&
    !me.isCall && !me.isCallReg && !me.isPushF && !me.isPopF &&
    !ldWait && !commitStoreReq && !issueMispredict
  private val popReq = meReady && me.isPop && !ldWait && !commitStoreReq && !issueMispredict
  private val retReq = meReady && me.isRet && !ldWait && !commitStoreReq && !issueMispredict
  private val popFReq = meReady && me.isPopF && !ldWait && !commitStoreReq && !issueMispredict
  private val loadFwd = loadReq && sbHitAny
  private val popFwd = popReq && sbHitAny
  private val retFwd = retReq && sbHitAny
  private val popFFwd = popFReq && sbHitAny
  private val anyFwd = loadFwd || popFwd || retFwd || popFFwd

  io.dmem.reqValid := commitStoreReq || ((loadReq || popReq || retReq || popFReq) && !anyFwd)
  io.dmem.reqWe    := commitStoreReq
  io.dmem.reqAddr  := Mux(commitStoreReq, headEntry.memAddr, mePaddr)
  io.dmem.reqWdata := Mux(commitStoreReq, headEntry.memData, 0.U(128.W))
  io.dmem.reqWmask := Mux(commitStoreReq,
    Mux(headEntry.memSize === 1.U, "h0001".U,
      Mux(headEntry.memSize === 2.U, "h0003".U,
        Mux(headEntry.memSize === 4.U, "h000f".U,
          Mux(headEntry.memSize === 8.U, "h00ff".U,
            Mux(headEntry.memSize === 16.U, "hffff".U, 0.U))))), 0.U)
  io.dmem.reqSize  := Mux(commitStoreReq, headEntry.memSize, me.memSize)

  when(commitStoreReq) {
    stWait := true.B
    stRobIdx := headSeq(4, 0)
    stSeq := headEntry.seq
  }

  when((loadReq || popReq || retReq || popFReq) && !anyFwd) {
    ldWait := true.B
    ldRobIdx := me.robIdx
    ldSeq := me.seq
    ldIsPop := me.isPop
    ldIsRet := me.isRet
    ldIsPopF := me.isPopF
  }

  // load completes from store buffer (no memory access)
  when(anyFwd) {
    val fwdVal = Mux(me.uop.op === Ops.MOV.U,
      mergedLoad(me.uop, sbHitData64, prfG(me.oldDst)),
      Mux(me.uop.op === Ops.MOVZX.U, zextLoad(me.uop, sbHitData64), sextLoad(me.uop, sbHitData64)))
    when(me.isAluRmw) {
      sb(sbFreeIdx).valid := true.B
      sb(sbFreeIdx).seq := me.seq
      sb(sbFreeIdx).addr := mePaddr
      sb(sbFreeIdx).data := aluRmwM.io.result.pad(128)
      sb(sbFreeIdx).size := me.memSize
      sb(sbFreeIdx).robIdx := me.robIdx
      rob(me.robIdx).memAddr := mePaddr
      rob(me.robIdx).memData := aluRmwM.io.result.pad(128)
      rob(me.robIdx).memSize := me.memSize
      when(me.uop.op === Ops.XCHG.U || me.uop.op === Ops.XADD.U) {
        prfG(me.pdst) := Mux(sbHitAny, sbHitData64, io.dmem.respData(63, 0))
        busyG(me.pdst) := false.B
      }
      when(me.hasDstF) {
        prfF(me.pdstF) := aluRmwM.io.flags
        busyF(me.pdstF) := false.B
      }
      rob(me.robIdx).done := true.B
    }.elsewhen(me.isAluMem) {
      when(me.hasDst) { prfG(me.pdst) := aluMerge(aluM.io.result); busyG(me.pdst) := false.B }
      when(me.hasDstF) { prfF(me.pdstF) := aluM.io.flags; busyF(me.pdstF) := false.B }
      rob(me.robIdx).done := true.B
    }.elsewhen(me.isComisMem) {
      prfF(me.pdstF) := sseMem.io.flags
      busyF(me.pdstF) := false.B
      rob(me.robIdx).done := true.B
    }.elsewhen(me.isJmpMem) {
      val tgt = sbHitData64
      rob(me.robIdx).value := tgt
      rob(me.robIdx).taken := true.B
      rob(me.robIdx).target := tgt
      rob(me.robIdx).done := true.B
      mqFlush := true.B
      mqFlushRob := me.robIdx
      mqFlushSeq := me.seq
      mqFlushTarget := tgt
    }.elsewhen(me.isPopF) {
      prfF(me.pdstF) := popfFlags(sbHitData64)
      busyF(me.pdstF) := false.B
      prfG(me.pdst2) := prfG(me.psrcRsp) + me.memSize
      busyG(me.pdst2) := false.B
      rob(me.robIdx).done := true.B
    }.elsewhen(me.hasDstX) {
      prfX(me.pdstX) := Mux(me.isSseArith, sseMem.io.result, sseLoadValue(sbHitData))
      busyX(me.pdstX) := false.B
      rob(me.robIdx).done := true.B
    }.elsewhen(me.isRet) {
      val tgt = sbHitData
      prfG(me.pdst2) := prfG(me.psrcRsp) + me.memSize
      busyG(me.pdst2) := false.B
      rob(me.robIdx).value := tgt
      rob(me.robIdx).taken := true.B
      rob(me.robIdx).target := tgt
      mqFlush := true.B
      mqFlushRob := me.robIdx
      mqFlushSeq := me.seq
      mqFlushTarget := tgt
    }.elsewhen(me.isPop) {
      prfG(me.pdst) := mergedLoad(me.uop, sbHitData64, prfG(me.oldDst))
      prfG(me.pdst2) := prfG(me.psrcRsp) + me.memSize
      busyG(me.pdst2) := false.B
      rob(me.robIdx).value := mergedLoad(me.uop, sbHitData64, prfG(me.oldDst))
    }.otherwise {
      prfG(me.pdst) := fwdVal
      rob(me.robIdx).value := fwdVal
    }
    when(me.hasDst) { busyG(me.pdst) := false.B }
    rob(me.robIdx).done := true.B
    when(!me.isRet && !me.isJmpMem) {
      mq(mqHead).valid := false.B
      mqPop := true.B
    }
  }

  when(io.dmem.respValid) {
    when(stWait) {
      stWait := false.B
      when(rob(stRobIdx).valid && rob(stRobIdx).seq === stSeq) {
        rob(stRobIdx).storeDone := true.B
      }
      for (k <- 0 until 8) {
        when(sb(k).valid && sb(k).robIdx === stRobIdx) { sb(k).valid := false.B }
      }
    }.elsewhen(ldWait) {
      ldWait := false.B
      when(rob(ldRobIdx).valid && rob(ldRobIdx).seq === ldSeq) {
        when(me.isPopF) {
      prfF(me.pdstF) := popfFlags(sbHitData64)
      busyF(me.pdstF) := false.B
      prfG(me.pdst2) := prfG(me.psrcRsp) + me.memSize
      busyG(me.pdst2) := false.B
      rob(me.robIdx).done := true.B
    }.elsewhen(me.hasDstX) {
          prfX(me.pdstX) := Mux(me.isSseArith, sseMem.io.result, sseLoadValue(io.dmem.respData))
          busyX(me.pdstX) := false.B
          rob(ldRobIdx).done := true.B
        }.elsewhen(me.isAluRmw) {
          sb(sbFreeIdx).valid := true.B
          sb(sbFreeIdx).seq := me.seq
          sb(sbFreeIdx).addr := mePaddr
          sb(sbFreeIdx).data := aluRmwM.io.result.pad(128)
          sb(sbFreeIdx).size := me.memSize
          sb(sbFreeIdx).robIdx := me.robIdx
          rob(me.robIdx).memAddr := mePaddr
          rob(me.robIdx).memData := aluRmwM.io.result.pad(128)
          rob(me.robIdx).memSize := me.memSize
          when(me.uop.op === Ops.XCHG.U || me.uop.op === Ops.XADD.U) {
            prfG(me.pdst) := io.dmem.respData(63, 0)
            busyG(me.pdst) := false.B
          }
          when(me.hasDstF) {
            prfF(me.pdstF) := aluRmwM.io.flags
            busyF(me.pdstF) := false.B
          }
          rob(ldRobIdx).done := true.B
        }.elsewhen(me.isAluMem) {
          when(me.hasDst) { prfG(me.pdst) := aluMerge(aluM.io.result); busyG(me.pdst) := false.B }
          when(me.hasDstF) { prfF(me.pdstF) := aluM.io.flags; busyF(me.pdstF) := false.B }
          rob(ldRobIdx).done := true.B
        }.elsewhen(me.isComisMem) {
          prfF(me.pdstF) := sseMem.io.flags
          busyF(me.pdstF) := false.B
          rob(ldRobIdx).done := true.B
        }.elsewhen(ldIsRet) {
          val tgt = io.dmem.respData
          prfG(me.pdst2) := prfG(me.psrcRsp) + me.memSize
          busyG(me.pdst2) := false.B
          rob(ldRobIdx).value := tgt
          rob(ldRobIdx).taken := true.B
          rob(ldRobIdx).target := tgt
          mqFlush := true.B
          mqFlushRob := me.robIdx
          mqFlushSeq := me.seq
          mqFlushTarget := tgt
        }.elsewhen(ldIsPop) {
          prfG(me.pdst) := mergedLoad(me.uop, io.dmem.respData, prfG(me.oldDst))
          prfG(me.pdst2) := prfG(me.psrcRsp) + me.memSize
          busyG(me.pdst2) := false.B
          rob(ldRobIdx).value := mergedLoad(me.uop, io.dmem.respData, prfG(me.oldDst))
        }.otherwise {
          prfG(me.pdst) := loadValue
          rob(ldRobIdx).value := loadValue
        }
        when(me.hasDst) { busyG(me.pdst) := false.B }
        rob(ldRobIdx).done := true.B
        when(!ldIsRet && !me.isJmpMem) {
          mq(mqHead).valid := false.B
          mqPop := true.B
        }
      }
    }
  }

  // push executes: decrement RSP, buffer store until commit
  when(meReady && (me.isPush || me.isCall || me.isCallReg || me.isPushF) &&
    sbFreeOk && !stWait && !issueMispredict) {
    val pushRsp = prfG(me.psrcRsp) - me.memSize
    val pushData = Mux(me.isPushF, prfF(me.psrcF),
      Mux(me.isCall || me.isCallReg, me.uop.pc + me.uop.len, meData))
    prfG(me.pdst2) := pushRsp
    busyG(me.pdst2) := false.B
    rob(me.robIdx).memAddr := mePaddr
    rob(me.robIdx).memData := pushData.pad(128)
    rob(me.robIdx).memSize := me.memSize
    rob(me.robIdx).done := true.B
    rob(me.robIdx).value := pushData
    when(me.isCall) {
      rob(me.robIdx).taken := true.B
      rob(me.robIdx).target := me.uop.pc + me.uop.len + me.uop.imm
    }
    when(me.isCallReg) {
      rob(me.robIdx).taken := true.B
      rob(me.robIdx).target := prfG(me.psrcTarget)
      mqFlush := true.B
      mqFlushRob := me.robIdx
      mqFlushSeq := me.seq
      mqFlushTarget := prfG(me.psrcTarget)
    }
    sb(sbFreeIdx).valid := true.B
    sb(sbFreeIdx).seq := me.seq
    sb(sbFreeIdx).addr := mePaddr
    sb(sbFreeIdx).data := pushData.pad(128)
    sb(sbFreeIdx).size := me.memSize
    sb(sbFreeIdx).robIdx := me.robIdx
    when(!me.isCallReg) {
      mq(mqHead).valid := false.B
      mqPop := true.B
    }
  }

  // store executes: record address/data and buffer it until commit
  when(meReady && me.isStore && sbFreeOk && !stWait && !issueMispredict) {
    rob(me.robIdx).memAddr := mePaddr
    rob(me.robIdx).memData := meData128
    rob(me.robIdx).memSize := Mux(me.isSseMem, sseSizeOf(me.uop), me.memSize)
    rob(me.robIdx).done := true.B
    rob(me.robIdx).value := meData128(63, 0)
    sb(sbFreeIdx).valid := true.B
    sb(sbFreeIdx).seq := me.seq
    sb(sbFreeIdx).addr := mePaddr
    sb(sbFreeIdx).data := meData128
    sb(sbFreeIdx).size := me.memSize
    sb(sbFreeIdx).robIdx := me.robIdx
    mq(mqHead).valid := false.B
    mqPop := true.B
  }

  // ------------------------------------------------------- ret redirect/flush
  when(mqFlush) {
    val fe = rob(mqFlushRob)
    ratG := fe.cpRatG
    ratF := fe.cpRatF
    ratX := fe.cpRatX
    when(fe.uop.op === Ops.RET.U ||
      (fe.uop.op === Ops.CALL.U && fe.uop.a.kind === OperandKind.Gpr.U)) {
      ratG(4) := fe.pdst2
    }
    for (k <- 0 until RobSize) {
      when(rob(k).valid && rob(k).seq > mqFlushSeq) {
        rob(k).valid := false.B
        when(rob(k).hasDst) { freeG(rob(k).pdst) := true.B }
        when(rob(k).hasDstF) { freeF(rob(k).pdstF) := true.B }
        when(rob(k).hasDstX) { freeX(rob(k).pdstX) := true.B }
        when(rob(k).hasDst2) { freeG(rob(k).pdst2) := true.B }
      }
    }
    for (k <- 0 until IqSize) {
      when(iq(k).valid && iq(k).seq > mqFlushSeq) { iq(k).valid := false.B }
    }
    for (k <- 0 until 8) {
      when(mq(k).valid && mq(k).seq > mqFlushSeq) { mq(k).valid := false.B }
      when(sb(k).valid && sb(k).seq > mqFlushSeq) { sb(k).valid := false.B }
    }
    mqHead := mqTail
    mqCount := 0.U
    xlDone := false.B
    xlWait := false.B
    tailSeq := mqFlushSeq + 1.U
    pc := mqFlushTarget
    ibufValid := false.B
    fetchStop := false.B
    fState := 0.U
    when(ldWait && ldSeq > mqFlushSeq) { ldWait := false.B }
    when(stWait && stSeq > mqFlushSeq) { stWait := false.B }
  }

  // ------------------------------------------------------------------- commit
  private val headIdx = headSeq(4, 0)
  private val he = rob(headIdx)
  private val commitOk = he.valid && he.done && he.seq === headSeq &&
    headSeq =/= tailSeq && !haltedReg && !fatalReg && !mqFlush && (!he.isStore || he.storeDone)

  private val headIdx1 = (headSeq + 1.U)(4, 0)
  private val he1 = rob(headIdx1)
  private val commitOk1 = commitOk && he.uop.op =/= Ops.HLT.U &&
    he1.valid && he1.done && he1.seq === (headSeq + 1.U) &&
    (!he1.isStore || he1.storeDone)

  when(commitOk) {
    when(he.hasDst) { freeG(he.oldDst) := true.B }
    when(he.hasDstF) { freeF(he.oldDstF) := true.B }
    when(he.hasDstX) { freeX(he.oldDstX) := true.B }
    when(he.hasDst2) { freeG(he.oldDst2) := true.B }
    rob(headIdx).valid := false.B
    when(he.uop.op === Ops.HLT.U) { haltedReg := true.B }
  }
  when(commitOk1) {
    when(he1.hasDst) { freeG(he1.oldDst) := true.B }
    when(he1.hasDstF) { freeF(he1.oldDstF) := true.B }
    when(he1.hasDstX) { freeX(he1.oldDstX) := true.B }
    when(he1.hasDst2) { freeG(he1.oldDst2) := true.B }
    rob(headIdx1).valid := false.B
    when(he1.uop.op === Ops.HLT.U) { haltedReg := true.B }
  }
  headSeq := headSeq + commitOk.asUInt + commitOk1.asUInt

  // --------------------------------------------------------------------- dbg
  for (i <- 0 until 16) { io.dbg.gpr(i) := prfG(ratG(i)) }
  io.dbg.flags := prfF(ratF(0))
  for (i <- 0 until 16) { io.dbg.xmm(i) := prfX(ratX(i)) }
  io.dbg.rip := pc
  io.dbg.headSeq := headSeq
  io.dbg.tailSeq := tailSeq
  io.dbg.iqUsed := iqUsed(7, 0)
  io.dbg.canUse := canUse
  io.dbg.dispatch := dispatchOk
  io.dbg.issue := issueValid
  io.dbg.busyLow := busyG.asUInt(31, 0)
  io.dbg.iqReady := iqReady.asUInt
  io.dbg.issuePc := ie.uop.pc
  io.dbg.issueRob := ie.robIdx
  for (i <- 0 until IqSize) {
    io.dbg.iqInfo(i) := Cat(iq(i).seq(15, 0), iq(i).psrcB, iq(i).psrcA,
      iqReady(i), iq(i).needB, iq(i).needA, iq(i).valid)
  }
  io.dbg.headValid := he.valid
  io.dbg.headDone := he.done
  io.dbg.headPc := he.uop.pc
  io.dbg.headEntrySeq := he.seq
  io.dbg.mqCountDbg := mqCount
  io.dbg.mqHeadDbg := mqHead
  io.dbg.mqTailDbg := mqTail
  io.dbg.meValidDbg := me.valid
  io.dbg.mePcDbg := me.uop.pc
  io.dbg.meStoreDbg := me.isStore
  io.dbg.ldWaitDbg := ldWait
  io.dbg.stWaitDbg := stWait
  io.dbg.sbMaskDbg := sb.map(_.valid.asUInt).reduce(_ ## _)
  io.dbg.flushTargetDbg := mqFlushTarget
  io.dbg.meOpDbg := me.uop.op
  io.dbg.headSizeDbg := he.memSize
  io.dbg.meSizeDbg := me.memSize
  io.dbg.headDataLoDbg := he.memData(63, 0)
  io.dbg.decOpDbg := dUop.op
  io.dbg.decAKindDbg := dUop.a.kind
  io.dbg.decBKindDbg := dUop.b.kind
  io.dbg.decMoveDbg := dSseMemMove
  io.dbg.decSizeDbg := dSseMemSize
  io.dbg.cr0Dbg := cr0
  io.dbg.cr3Dbg := cr3
  io.dbg.movCrWriteDbg := ieMovCrWrite
  io.dbg.movCrValDbg := rsA
  io.dbg.meVaddrDbg := meVaddr
  io.dbg.mePaddrDbg := mePaddr
  io.dbg.xlatDbg := Cat(xlDone, xlWait, mmu.io.reqValid, mmu.io.done)
  io.dbg.ptwDataDbg := mmu.io.ptwData
  io.dbg.ptwValidDbg := mmu.io.ptwValid
  io.dbg.mmuFaultDbg := mmu.io.fault
  io.dbg.ptwAddrDbg := mmu.io.ptwAddr
  io.dbg.divOpDbg := Mux(issueValid && ieMulDiv, ie.uop.op, 0.U)
  io.dbg.divADbg := Mux(issueValid && ieMulDiv, rsA, 0.U)
  io.dbg.divBDbg := Mux(issueValid && ieMulDiv, rsB, 0.U)
  io.dbg.divHiDbg := Mux(issueValid && ieMulDiv, prfG(ie.psrcC), 0.U)
  io.dbg.divOpszDbg := Mux(issueValid && ieMulDiv, ie.uop.opsz, 0.U)
  io.dbg.divErrDbg := Mux(issueValid && ieMulDiv, alu.io.divError, false.B)
  io.dbg.decIllegalDbg := dec.io.illegal
  io.dbg.canDispatchDbg := canDispatch
  io.dbg.isCdqDbg := dIsCdq
  io.dbg.issue2Cnt := issue2Cnt
  io.dbg.issue1Dbg := issueValid1
  io.dbg.meJmpMemDbg := me.isJmpMem
  io.dbg.anyFwdDbg := anyFwd
  io.dbg.loadReqDbg := loadReq
  io.dbg.meReadyDbg := meReady
  io.dbg.meComisDbg := me.isComisMem
  io.dbg.meHasDstFDbg := me.hasDstF
  io.dbg.mePdstFDbg := me.pdstF
  io.dbg.decNeedFDbg := needF
  io.dbg.decPdstFDbg := newPdstF
  io.dbg.ratFDbg := ratF(0)
  io.dbg.f0Dbg := prfF(0)
  io.dbg.f1Dbg := prfF(1)
  io.dbg.f2Dbg := prfF(2)
  io.dbg.f3Dbg := prfF(3)
  for (i <- 0 until 8) {
    io.dbg.mqInfoDbg(i) := Cat(me.isRet, me.isCall, me.isPush, me.isPop,
      mq(i).isRet, mq(i).isCall, mq(i).isPush, mq(i).isPop,
      mq(i).pdst2, mq(i).psrcRsp, mq(i).seq(15, 0), mq(i).valid, mq(i).uop.pc(31, 0))
  }
  io.dbg.cycles := cycleCnt
  when(!(ieMovCrWrite && crIdx === 3.U)) { cr3Flush := false.B }
  when(!issueMispredict && !mqFlush) {
    when(mqPop) {
      mqHead := mqHead + 1.U
      xlDone := false.B
      xlWait := false.B
    }
    mqCount := mqCount + mqPush.asUInt - mqPop.asUInt
  }

  // RET / indirect-CALL redirect: the branch is at the MQ head and all
  // remaining entries are younger, so drop the whole MQ and reset its pointers.
  when(mqFlush) {
    for (k <- 0 until 8) { mq(k).valid := false.B }
    mqHead := 0.U
    mqTail := 0.U
    mqCount := 0.U
  }

  io.halted := haltedReg
  io.exitCode := 0.U
  io.fatal := fatalReg
  io.fatalPc := fatalPcReg
  io.fatalCode := fatalCdReg
}
