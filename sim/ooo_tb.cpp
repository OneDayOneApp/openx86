#include "VX86OooTop.h"
#include "verilated.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static const uint64_t MEM_SIZE = 16ull * 1024 * 1024;
static const uint64_t MEM_MASK = MEM_SIZE - 1;
static std::vector<uint8_t> mem(MEM_SIZE, 0);

static uint8_t lb(uint64_t a) { return mem[a & MEM_MASK]; }

static uint64_t fnv1a(const uint8_t *p, size_t n, uint64_t h = 1469598103934665603ull) {
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}

static uint64_t memhash() {
  uint64_t h = 1469598103934665603ull;
  h = fnv1a(&mem[0x7f0000 & MEM_MASK], 0x10000, h);
  h = fnv1a(&mem[0x600000 & MEM_MASK], 0x10000, h);
  return h;
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  if (argc < 2) { std::fprintf(stderr, "usage: %s <image.bin> [max_cycles]\n", argv[0]); return 2; }
  uint64_t maxCycles = (argc > 2) ? strtoull(argv[2], nullptr, 0) : 2000000ull;
  std::ifstream img(argv[1], std::ios::binary);
  std::vector<unsigned char> image((std::istreambuf_iterator<char>(img)), std::istreambuf_iterator<char>());
  if (!img) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
  std::memcpy(&mem[0x400000], image.data(), image.size());

  VX86OooTop dut;
  dut.clock = 0; dut.reset = 1;
  dut.io_imem_resp_valid = 0;
  for (int w = 0; w < 8; w++) dut.io_imem_resp_data[w] = 0;
  dut.eval();
  dut.clock = 1; dut.eval();
  dut.clock = 0; dut.reset = 0; dut.eval();

  bool imPend = false; uint64_t imAddr = 0;
  bool dmPend = false; uint64_t dmAddr = 0;
  bool ptPend = false; uint64_t ptAddr = 0;
  bool ipPend = false; uint64_t ipAddr = 0;
  uint64_t cycle = 0; bool halted = false, fatal = false;

  for (; cycle < maxCycles; cycle++) {
    dut.io_dmem_resp_valid = dmPend ? 1 : 0;
    if (dmPend) {
      for (int w = 0; w < 4; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4; b++) v |= (uint32_t)lb(dmAddr + 4 * w + b) << (8 * b);
        dut.io_dmem_resp_data[w] = v;
      }
    }
    dut.io_iptw_resp_valid = ipPend ? 1 : 0;
    if (ipPend) {
      uint64_t v = 0;
      for (int b = 0; b < 8; b++) v |= (uint64_t)lb(ipAddr + b) << (8 * b);
      dut.io_iptw_resp_data = v;
    }
    dut.io_ptw_resp_valid = ptPend ? 1 : 0;
    if (ptPend) {
      uint64_t v = 0;
      for (int b = 0; b < 8; b++) v |= (uint64_t)lb(ptAddr + b) << (8 * b);
      dut.io_ptw_resp_data = v;
      if (getenv("TRACE"))
        std::printf("  PTW-RESP addr=0x%llx data=0x%llx\n", (unsigned long long)ptAddr,
                    (unsigned long long)v);
    }
    dut.io_imem_resp_valid = imPend ? 1 : 0;
    if (imPend) {
      for (int w = 0; w < 8; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4; b++) v |= (uint32_t)lb(imAddr + 4 * w + b) << (8 * b);
        dut.io_imem_resp_data[w] = v;
      }
    }
    dut.clock = 0; dut.eval();
    bool ptReq = dut.io_ptw_req_valid;
    uint64_t ptReqAddr = dut.io_ptw_req_addr;
    bool ipReq = dut.io_iptw_req_valid;
    uint64_t ipReqAddr = dut.io_iptw_req_addr;
    if (getenv("TRACE") && ptReq)
      std::printf("  PTW cyc=%llu addr=0x%llx\n", (unsigned long long)cycle, (unsigned long long)ptReqAddr);
    bool imReq = dut.io_imem_req_valid;
    uint64_t imReqAddr = dut.io_imem_req_addr;
    bool dmReq = dut.io_dmem_req_valid;
    bool dmWe = dut.io_dmem_req_we;
    uint64_t dmReqAddr = dut.io_dmem_req_addr;
    unsigned dmMask = (unsigned)dut.io_dmem_req_wmask;
    uint32_t dmWords[4] = {dut.io_dmem_req_wdata[0], dut.io_dmem_req_wdata[1],
                           dut.io_dmem_req_wdata[2], dut.io_dmem_req_wdata[3]};
    dut.clock = 1; dut.eval();
    dut.clock = 0; dut.eval();
    if (getenv("TRACE"))
      std::printf("T cyc=%llu pc=0x%llx can=%d disp=%d iss=%d head=%u tail=%u iq=%u halt=%d fatal=%d\n",
                  (unsigned long long)cycle, (unsigned long long)dut.io_dbg_rip,
                  (int)dut.io_dbg_canUse, (int)dut.io_dbg_dispatch, (int)dut.io_dbg_issue,
                  (unsigned)dut.io_dbg_headSeq, (unsigned)dut.io_dbg_tailSeq,
                  (unsigned)dut.io_dbg_iqUsed, (int)dut.io_halted, (int)dut.io_fatal);
    if (getenv("TRACE"))
      std::printf("  busy=0x%08x ready=0x%02x issuePc=0x%llx rob=%u\n",
                  (unsigned)dut.io_dbg_busyLow, (unsigned)dut.io_dbg_iqReady,
                  (unsigned long long)dut.io_dbg_issuePc, (unsigned)dut.io_dbg_issueRob);
    if (getenv("TRACE"))
      std::printf("  iq=%08x %08x %08x %08x %08x %08x %08x %08x\n",
                  (unsigned)dut.io_dbg_iqInfo_0, (unsigned)dut.io_dbg_iqInfo_1,
                  (unsigned)dut.io_dbg_iqInfo_2, (unsigned)dut.io_dbg_iqInfo_3,
                  (unsigned)dut.io_dbg_iqInfo_4, (unsigned)dut.io_dbg_iqInfo_5,
                  (unsigned)dut.io_dbg_iqInfo_6, (unsigned)dut.io_dbg_iqInfo_7);
    if (getenv("TRACE"))
      std::printf("  head v=%d d=%d pc=0x%llx seq=%u\n", (int)dut.io_dbg_headValid,
                  (int)dut.io_dbg_headDone, (unsigned long long)dut.io_dbg_headPc,
                  (unsigned)dut.io_dbg_headEntrySeq);
    if (getenv("TRACE"))
      std::printf("  mq cnt=%u h=%u t=%u me v=%d pc=0x%llx st=%d ldw=%d stw=%d sb=0x%02x\n",
                  (unsigned)dut.io_dbg_mqCountDbg, (unsigned)dut.io_dbg_mqHeadDbg,
                  (unsigned)dut.io_dbg_mqTailDbg, (int)dut.io_dbg_meValidDbg,
                  (unsigned long long)dut.io_dbg_mePcDbg, (int)dut.io_dbg_meStoreDbg,
                  (int)dut.io_dbg_ldWaitDbg, (int)dut.io_dbg_stWaitDbg,
                  (unsigned)dut.io_dbg_sbMaskDbg);
    if (getenv("TRACE"))
      if (getenv("TRACE"))
        std::printf("  jmpMem=%d anyFwd=%d loadReq=%d meReady=%d\n", (int)dut.io_dbg_meJmpMemDbg,
                    (int)dut.io_dbg_anyFwdDbg, (int)dut.io_dbg_loadReqDbg, (int)dut.io_dbg_meReadyDbg);
    if (getenv("TRACE"))
        std::printf("  flags=0x%llx\n", (unsigned long long)dut.io_dbg_flags);
    if (getenv("TRACE"))
        std::printf("  comis=%d meHasDstF=%d mePdstF=%u decNeedF=%d decPdstF=%u\n",
                    (int)dut.io_dbg_meComisDbg, (int)dut.io_dbg_meHasDstFDbg,
                    (unsigned)dut.io_dbg_mePdstFDbg, (int)dut.io_dbg_decNeedFDbg,
                    (unsigned)dut.io_dbg_decPdstFDbg);
    if (getenv("TRACE"))
      std::printf("  illegal=%d canDisp=%d isCdq=%d\n", (int)dut.io_dbg_decIllegalDbg,
                    (int)dut.io_dbg_canDispatchDbg, (int)dut.io_dbg_isCdqDbg);
    if (dut.io_dbg_divOpDbg)
        std::printf("  DIV op=%u a=0x%llx b=0x%llx hi=0x%llx sz=%u err=%d\n",
                    (unsigned)dut.io_dbg_divOpDbg, (unsigned long long)dut.io_dbg_divADbg,
                    (unsigned long long)dut.io_dbg_divBDbg, (unsigned long long)dut.io_dbg_divHiDbg,
                    (unsigned)dut.io_dbg_divOpszDbg, (int)dut.io_dbg_divErrDbg);
    if (getenv("TRACE"))
      std::printf("  ptwAddr=0x%llx ptwValid=%d ptwData=0x%llx mmuFault=%d\n",
                  (unsigned long long)dut.io_dbg_ptwAddrDbg, (int)dut.io_dbg_ptwValidDbg,
                  (unsigned long long)dut.io_dbg_ptwDataDbg, (int)dut.io_dbg_mmuFaultDbg);
    if (getenv("TRACE"))
      std::printf("  mV=0x%llx mP=0x%llx xlat=0x%x\n",
                  (unsigned long long)dut.io_dbg_meVaddrDbg,
                  (unsigned long long)dut.io_dbg_mePaddrDbg, (unsigned)dut.io_dbg_xlatDbg);
    if (getenv("TRACE"))
      std::printf("  cr0=0x%llx cr3=0x%llx movcr=%d val=0x%llx\n",
                  (unsigned long long)dut.io_dbg_cr0Dbg, (unsigned long long)dut.io_dbg_cr3Dbg,
                  (int)dut.io_dbg_movCrWriteDbg, (unsigned long long)dut.io_dbg_movCrValDbg);
    if (getenv("TRACE"))
      std::printf("  decOp=%u aK=%u bK=%u move=%d size=%u\n", (unsigned)dut.io_dbg_decOpDbg,
                  (unsigned)dut.io_dbg_decAKindDbg, (unsigned)dut.io_dbg_decBKindDbg,
                  (int)dut.io_dbg_decMoveDbg, (unsigned)dut.io_dbg_decSizeDbg);
    if (getenv("TRACE"))
      std::printf("  headSize=%u meSize=%u headDataLo=0x%llx\n", (unsigned)dut.io_dbg_headSizeDbg,
                  (unsigned)dut.io_dbg_meSizeDbg, (unsigned long long)dut.io_dbg_headDataLoDbg);
    if (getenv("TRACE"))
      std::printf("  meOp=%u mq=%016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx\n",
                  (unsigned)dut.io_dbg_meOpDbg,
                  (unsigned long long)dut.io_dbg_mqInfoDbg_0,
                  (unsigned long long)dut.io_dbg_mqInfoDbg_1,
                  (unsigned long long)dut.io_dbg_mqInfoDbg_2,
                  (unsigned long long)dut.io_dbg_mqInfoDbg_3,
                  (unsigned long long)dut.io_dbg_mqInfoDbg_4,
                  (unsigned long long)dut.io_dbg_mqInfoDbg_5,
                  (unsigned long long)dut.io_dbg_mqInfoDbg_6,
                  (unsigned long long)dut.io_dbg_mqInfoDbg_7);
    if (getenv("TRACE"))
      std::printf("  flushTarget=0x%llx\n", (unsigned long long)dut.io_dbg_flushTargetDbg);
    if (getenv("TRACE") && dmReq)
      std::printf("  DMEM we=%d addr=0x%llx data=0x%llx mask=0x%x\n", (int)dmWe,
                  (unsigned long long)dmReqAddr, (unsigned long long)dmWords[0], dmMask);
    if (dmReq && dmWe) {
      for (int b = 0; b < 16; b++) {
        if (dmMask & (1u << b)) {
          uint32_t w = dmWords[b / 4];
          mem[(dmReqAddr + b) & MEM_MASK] = (uint8_t)(w >> (8 * (b % 4)));
        }
      }
    }
    dmPend = dmReq; dmAddr = dmReqAddr;
    ptPend = ptReq; ptAddr = ptReqAddr;
    ipPend = ipReq; ipAddr = ipReqAddr;
    imPend = imReq; imAddr = imReqAddr;
    halted = dut.io_halted; fatal = dut.io_fatal;
    if (dut.io_dbg_headSeq > dut.io_dbg_tailSeq) {
      std::fprintf(stderr, "invariant broken: head=%u tail=%u at cycle %llu\n",
                   (unsigned)dut.io_dbg_headSeq, (unsigned)dut.io_dbg_tailSeq,
                   (unsigned long long)cycle);
      return 4;
    }
    if (halted || fatal) { cycle++; break; }
  }

  uint64_t gpr[16];
  gpr[0]=dut.io_dbg_gpr_0; gpr[1]=dut.io_dbg_gpr_1; gpr[2]=dut.io_dbg_gpr_2; gpr[3]=dut.io_dbg_gpr_3;
  gpr[4]=dut.io_dbg_gpr_4; gpr[5]=dut.io_dbg_gpr_5; gpr[6]=dut.io_dbg_gpr_6; gpr[7]=dut.io_dbg_gpr_7;
  gpr[8]=dut.io_dbg_gpr_8; gpr[9]=dut.io_dbg_gpr_9; gpr[10]=dut.io_dbg_gpr_10; gpr[11]=dut.io_dbg_gpr_11;
  gpr[12]=dut.io_dbg_gpr_12; gpr[13]=dut.io_dbg_gpr_13; gpr[14]=dut.io_dbg_gpr_14; gpr[15]=dut.io_dbg_gpr_15;

  std::printf("{\"gpr\":[");
  for (int i = 0; i < 16; i++) std::printf("%s\"0x%016llx\"", i ? "," : "", (unsigned long long)gpr[i]);
  std::printf("],\"xmm\":[");
  for (int i = 0; i < 16; i++) {
    unsigned w3 = 0, w2 = 0, w1 = 0, w0 = 0;
    switch (i) {
      case 0: w0=dut.io_dbg_xmm_0[0]; w1=dut.io_dbg_xmm_0[1]; w2=dut.io_dbg_xmm_0[2]; w3=dut.io_dbg_xmm_0[3]; break;
      case 1: w0=dut.io_dbg_xmm_1[0]; w1=dut.io_dbg_xmm_1[1]; w2=dut.io_dbg_xmm_1[2]; w3=dut.io_dbg_xmm_1[3]; break;
      case 2: w0=dut.io_dbg_xmm_2[0]; w1=dut.io_dbg_xmm_2[1]; w2=dut.io_dbg_xmm_2[2]; w3=dut.io_dbg_xmm_2[3]; break;
      case 3: w0=dut.io_dbg_xmm_3[0]; w1=dut.io_dbg_xmm_3[1]; w2=dut.io_dbg_xmm_3[2]; w3=dut.io_dbg_xmm_3[3]; break;
      case 4: w0=dut.io_dbg_xmm_4[0]; w1=dut.io_dbg_xmm_4[1]; w2=dut.io_dbg_xmm_4[2]; w3=dut.io_dbg_xmm_4[3]; break;
      case 5: w0=dut.io_dbg_xmm_5[0]; w1=dut.io_dbg_xmm_5[1]; w2=dut.io_dbg_xmm_5[2]; w3=dut.io_dbg_xmm_5[3]; break;
      case 6: w0=dut.io_dbg_xmm_6[0]; w1=dut.io_dbg_xmm_6[1]; w2=dut.io_dbg_xmm_6[2]; w3=dut.io_dbg_xmm_6[3]; break;
      case 7: w0=dut.io_dbg_xmm_7[0]; w1=dut.io_dbg_xmm_7[1]; w2=dut.io_dbg_xmm_7[2]; w3=dut.io_dbg_xmm_7[3]; break;
      case 8: w0=dut.io_dbg_xmm_8[0]; w1=dut.io_dbg_xmm_8[1]; w2=dut.io_dbg_xmm_8[2]; w3=dut.io_dbg_xmm_8[3]; break;
      case 9: w0=dut.io_dbg_xmm_9[0]; w1=dut.io_dbg_xmm_9[1]; w2=dut.io_dbg_xmm_9[2]; w3=dut.io_dbg_xmm_9[3]; break;
      case 10: w0=dut.io_dbg_xmm_10[0]; w1=dut.io_dbg_xmm_10[1]; w2=dut.io_dbg_xmm_10[2]; w3=dut.io_dbg_xmm_10[3]; break;
      case 11: w0=dut.io_dbg_xmm_11[0]; w1=dut.io_dbg_xmm_11[1]; w2=dut.io_dbg_xmm_11[2]; w3=dut.io_dbg_xmm_11[3]; break;
      case 12: w0=dut.io_dbg_xmm_12[0]; w1=dut.io_dbg_xmm_12[1]; w2=dut.io_dbg_xmm_12[2]; w3=dut.io_dbg_xmm_12[3]; break;
      case 13: w0=dut.io_dbg_xmm_13[0]; w1=dut.io_dbg_xmm_13[1]; w2=dut.io_dbg_xmm_13[2]; w3=dut.io_dbg_xmm_13[3]; break;
      case 14: w0=dut.io_dbg_xmm_14[0]; w1=dut.io_dbg_xmm_14[1]; w2=dut.io_dbg_xmm_14[2]; w3=dut.io_dbg_xmm_14[3]; break;
      default: w0=dut.io_dbg_xmm_15[0]; w1=dut.io_dbg_xmm_15[1]; w2=dut.io_dbg_xmm_15[2]; w3=dut.io_dbg_xmm_15[3]; break;
    }
    std::printf("%s\"0x%08x%08x%08x%08x\"", i ? "," : "", w3, w2, w1, w0);
  }
  std::printf("],\"rip\":\"0x%016llx\",\"flags\":\"0x%016llx\",\"cycles\":%llu,\"issue2\":%u,"
              "\"memhash\":\"0x%016llx\",\"halted\":%d,\"fatal\":%d,\"exit\":%llu,"
              "\"fatalPc\":\"0x%016llx\",\"fatalCode\":%u}\n",
              (unsigned long long)dut.io_dbg_rip, (unsigned long long)dut.io_dbg_flags,
              (unsigned long long)dut.io_dbg_cycles, (unsigned)dut.io_dbg_issue2Cnt,
              (unsigned long long)memhash(),
              halted ? 1 : 0, fatal ? 1 : 0,
              (unsigned long long)dut.io_exitCode, (unsigned long long)dut.io_fatalPc,
              (unsigned)dut.io_fatalCode);
  return fatal ? 3 : 0;
}
