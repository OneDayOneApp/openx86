#include "VX86Top.h"
#include "verilated.h"

#include "difftest.cpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static const uint64_t MEM_SIZE = 16ull * 1024 * 1024;
static const uint64_t MEM_MASK = MEM_SIZE - 1;
static std::vector<uint8_t> mem(MEM_SIZE, 0);

static uint8_t lb(uint64_t a) { return mem[a & MEM_MASK]; }
static void sb(uint64_t a, uint8_t v) { mem[a & MEM_MASK] = v; }

static uint64_t ld64(uint64_t a) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= (uint64_t)lb(a + i) << (8 * i);
  return v;
}

static void st64(uint64_t a, uint64_t v) {
  for (int i = 0; i < 8; i++) sb(a + i, (uint8_t)(v >> (8 * i)));
}

static uint64_t fnv1a(const uint8_t *p, size_t n, uint64_t h = 1469598103934665603ull) {
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

static uint64_t memhash() {
  // Stack window and data window, same regions as the Python model.
  uint64_t h = 1469598103934665603ull;
  h = fnv1a(&mem[0x7f0000 & MEM_MASK], 0x10000, h);
  h = fnv1a(&mem[0x600000 & MEM_MASK], 0x10000, h);
  return h;
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  bool diff = false;
  const char *refSo = nullptr;
  const char *imagePath = nullptr;
  uint64_t maxCycles = 20000000ull;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--diff") == 0) {
      diff = true;
    } else if (std::strcmp(argv[i], "--no-diff") == 0) {
      diff = false;
    } else if (std::strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
      imagePath = argv[++i];
    } else if (std::strncmp(argv[i], "--image=", 8) == 0) {
      imagePath = argv[i] + 8;
    } else if (std::strncmp(argv[i], "--ref-so=", 9) == 0) {
      refSo = argv[i] + 9;
    } else if (std::strncmp(argv[i], "--max-cycles=", 13) == 0) {
      maxCycles = strtoull(argv[i] + 13, nullptr, 0);
    } else if (std::strncmp(argv[i], "--", 2) == 0) {
      std::fprintf(stderr, "unknown option: %s\n", argv[i]);
      return 2;
    } else if (!imagePath) {
      imagePath = argv[i];
    } else {
      // Legacy positional form: <image> [max_cycles]
      maxCycles = strtoull(argv[i], nullptr, 0);
    }
  }
  if (!imagePath) {
    std::fprintf(stderr,
                 "usage: %s [-i IMAGE] [--diff] [--ref-so=PATH] [--max-cycles=N]\n"
                 "       %s [--no-diff] [--image=PATH] [image.bin]\n",
                 argv[0], argv[0]);
    return 2;
  }

  std::ifstream img(imagePath, std::ios::binary);
  std::vector<unsigned char> image((std::istreambuf_iterator<char>(img)),
                                   std::istreambuf_iterator<char>());
  if (!img) {
    std::fprintf(stderr, "cannot open %s\n", imagePath);
    return 2;
  }
  std::memcpy(&mem[0x400000], image.data(), image.size());

  if (diff && !difftest_init(refSo ? refSo : "build/ref/libx86ref.so",
                             image.data(), image.size(), 0x400000))
    return 4;

  VX86Top dut;
  dut.clock = 0;
  dut.reset = 1;
  dut.io_imem_resp_valid = 0;
  for (int w = 0; w < 8; w++) dut.io_imem_resp_data[w] = 0;
  dut.io_dmem_resp_valid = 0;
  for (int w = 0; w < 4; w++) dut.io_dmem_resp_rdata[w] = 0;
  dut.eval();
  dut.clock = 1;
  dut.eval();
  dut.clock = 0;
  dut.reset = 0;
  dut.eval();

  bool imPend = false, dmPend = false, ptPend = false;
  uint64_t imAddr = 0, dmAddr = 0, ptAddr = 0;
  bool dmWe = false;
  uint32_t dmMask = 0;
  unsigned dmSize = 0;
  uint8_t dmData[16] = {0};

  uint64_t cycle = 0;
  bool halted = false, fatal = false;
  uint64_t arch_flags = 0x202;
  bool pend_valid = false;
  bool commit_prev = false;
  uint64_t commit_prev_pc = 0;
  uint64_t pend_pc = 0, pend_flags = 0;
  uint32_t pend_op = 0;

  auto call_diff = [&](uint64_t pc, uint64_t flags, uint32_t op) {
    uint64_t g[16] = {
        dut.io_dbg_gpr_0, dut.io_dbg_gpr_1, dut.io_dbg_gpr_2, dut.io_dbg_gpr_3,
        dut.io_dbg_gpr_4, dut.io_dbg_gpr_5, dut.io_dbg_gpr_6, dut.io_dbg_gpr_7,
        dut.io_dbg_gpr_8, dut.io_dbg_gpr_9, dut.io_dbg_gpr_10, dut.io_dbg_gpr_11,
        dut.io_dbg_gpr_12, dut.io_dbg_gpr_13, dut.io_dbg_gpr_14, dut.io_dbg_gpr_15};
    uint64_t xv[16][2];
    xv[0][0] = (uint64_t)dut.io_dbg_xmm_0[0] | ((uint64_t)dut.io_dbg_xmm_0[1] << 32);
    xv[0][1] = (uint64_t)dut.io_dbg_xmm_0[2] | ((uint64_t)dut.io_dbg_xmm_0[3] << 32);
    xv[1][0] = (uint64_t)dut.io_dbg_xmm_1[0] | ((uint64_t)dut.io_dbg_xmm_1[1] << 32);
    xv[1][1] = (uint64_t)dut.io_dbg_xmm_1[2] | ((uint64_t)dut.io_dbg_xmm_1[3] << 32);
    xv[2][0] = (uint64_t)dut.io_dbg_xmm_2[0] | ((uint64_t)dut.io_dbg_xmm_2[1] << 32);
    xv[2][1] = (uint64_t)dut.io_dbg_xmm_2[2] | ((uint64_t)dut.io_dbg_xmm_2[3] << 32);
    xv[3][0] = (uint64_t)dut.io_dbg_xmm_3[0] | ((uint64_t)dut.io_dbg_xmm_3[1] << 32);
    xv[3][1] = (uint64_t)dut.io_dbg_xmm_3[2] | ((uint64_t)dut.io_dbg_xmm_3[3] << 32);
    xv[4][0] = (uint64_t)dut.io_dbg_xmm_4[0] | ((uint64_t)dut.io_dbg_xmm_4[1] << 32);
    xv[4][1] = (uint64_t)dut.io_dbg_xmm_4[2] | ((uint64_t)dut.io_dbg_xmm_4[3] << 32);
    xv[5][0] = (uint64_t)dut.io_dbg_xmm_5[0] | ((uint64_t)dut.io_dbg_xmm_5[1] << 32);
    xv[5][1] = (uint64_t)dut.io_dbg_xmm_5[2] | ((uint64_t)dut.io_dbg_xmm_5[3] << 32);
    xv[6][0] = (uint64_t)dut.io_dbg_xmm_6[0] | ((uint64_t)dut.io_dbg_xmm_6[1] << 32);
    xv[6][1] = (uint64_t)dut.io_dbg_xmm_6[2] | ((uint64_t)dut.io_dbg_xmm_6[3] << 32);
    xv[7][0] = (uint64_t)dut.io_dbg_xmm_7[0] | ((uint64_t)dut.io_dbg_xmm_7[1] << 32);
    xv[7][1] = (uint64_t)dut.io_dbg_xmm_7[2] | ((uint64_t)dut.io_dbg_xmm_7[3] << 32);
    xv[8][0] = (uint64_t)dut.io_dbg_xmm_8[0] | ((uint64_t)dut.io_dbg_xmm_8[1] << 32);
    xv[8][1] = (uint64_t)dut.io_dbg_xmm_8[2] | ((uint64_t)dut.io_dbg_xmm_8[3] << 32);
    xv[9][0] = (uint64_t)dut.io_dbg_xmm_9[0] | ((uint64_t)dut.io_dbg_xmm_9[1] << 32);
    xv[9][1] = (uint64_t)dut.io_dbg_xmm_9[2] | ((uint64_t)dut.io_dbg_xmm_9[3] << 32);
    xv[10][0] = (uint64_t)dut.io_dbg_xmm_10[0] | ((uint64_t)dut.io_dbg_xmm_10[1] << 32);
    xv[10][1] = (uint64_t)dut.io_dbg_xmm_10[2] | ((uint64_t)dut.io_dbg_xmm_10[3] << 32);
    xv[11][0] = (uint64_t)dut.io_dbg_xmm_11[0] | ((uint64_t)dut.io_dbg_xmm_11[1] << 32);
    xv[11][1] = (uint64_t)dut.io_dbg_xmm_11[2] | ((uint64_t)dut.io_dbg_xmm_11[3] << 32);
    xv[12][0] = (uint64_t)dut.io_dbg_xmm_12[0] | ((uint64_t)dut.io_dbg_xmm_12[1] << 32);
    xv[12][1] = (uint64_t)dut.io_dbg_xmm_12[2] | ((uint64_t)dut.io_dbg_xmm_12[3] << 32);
    xv[13][0] = (uint64_t)dut.io_dbg_xmm_13[0] | ((uint64_t)dut.io_dbg_xmm_13[1] << 32);
    xv[13][1] = (uint64_t)dut.io_dbg_xmm_13[2] | ((uint64_t)dut.io_dbg_xmm_13[3] << 32);
    xv[14][0] = (uint64_t)dut.io_dbg_xmm_14[0] | ((uint64_t)dut.io_dbg_xmm_14[1] << 32);
    xv[14][1] = (uint64_t)dut.io_dbg_xmm_14[2] | ((uint64_t)dut.io_dbg_xmm_14[3] << 32);
    xv[15][0] = (uint64_t)dut.io_dbg_xmm_15[0] | ((uint64_t)dut.io_dbg_xmm_15[1] << 32);
    xv[15][1] = (uint64_t)dut.io_dbg_xmm_15[2] | ((uint64_t)dut.io_dbg_xmm_15[3] << 32);
    difftest_step(pc, g, xv, flags, op);
  };

  auto process_pending = [&]() {
    if (!diff || !pend_valid) return;
    call_diff(pend_pc, pend_flags, pend_op);
    pend_valid = false;
  };

  for (; cycle < maxCycles; cycle++) {
    // Drive responses for requests issued in the previous cycle.
    dut.io_imem_resp_valid = imPend ? 1 : 0;
    if (imPend) {
      for (int w = 0; w < 8; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4; b++) v |= (uint32_t)lb(imAddr + 4 * w + b) << (8 * b);
        dut.io_imem_resp_data[w] = v;
      }
    }
    dut.io_ptw_resp_valid = ptPend ? 1 : 0;
    if (ptPend) {
      uint64_t v = 0;
      for (int i = 0; i < 8; i++) v |= (uint64_t)lb(ptAddr + i) << (8 * i);
      dut.io_ptw_resp_data = v;
    }
    dut.io_dmem_resp_valid = dmPend ? 1 : 0;
    if (dmPend) {
      if (dmWe) {
        for (int i = 0; i < 16; i++)
          if (dmMask & (1u << i)) sb(dmAddr + i, dmData[i]);
        // Committed stores are replayed by the reference model itself; pushing
        // them early would corrupt RMW/XCHG semantics.
      }
      for (int w = 0; w < 4; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4; b++) v |= (uint32_t)lb(dmAddr + 4 * w + b) << (8 * b);
        dut.io_dmem_resp_rdata[w] = v;
      }
      if (diff && !dmWe) {
        uint8_t data[16] = {0};
        for (int w = 0; w < 4; w++)
          for (int b = 0; b < 4; b++) data[4 * w + b] = (uint8_t)(dut.io_dmem_resp_rdata[w] >> (8 * b));
        uint32_t mask = (dmSize >= 8) ? 0xffu : ((1u << dmSize) - 1u);
        difftest_check_load(dmAddr, data, mask);
      }
      if (getenv("TRACE"))
        std::printf("R addr=0x%llx we=%d q0=0x%llx\n", (unsigned long long)dmAddr,
                    (int)dmWe, (unsigned long long)((uint64_t)dut.io_dmem_resp_rdata[0] |
                    ((uint64_t)dut.io_dmem_resp_rdata[1] << 32)));
    }

    dut.clock = 0;
    dut.eval();

    bool imReq = dut.io_imem_req_valid;
    if (getenv("TRACE") && imReq)
      std::printf("F pc=0x%llx line=0x%llx\n", (unsigned long long)dut.io_dbg_rip,
                  (unsigned long long)dut.io_imem_req_addr);
    uint64_t imReqAddr = dut.io_imem_req_addr;
    bool ptReq = dut.io_ptw_req_valid;
    uint64_t ptReqAddr = dut.io_ptw_req_addr;
    if (getenv("TRACE") && ptReq)
      std::printf("  PTW addr=0x%llx\n", (unsigned long long)ptReqAddr);
    bool dmReq = dut.io_dmem_req_valid;
    uint64_t dmReqAddr = dut.io_dmem_req_addr;
    bool dmReqWe = dut.io_dmem_req_we;
    uint32_t dmReqMask = dut.io_dmem_req_wmask;
    uint32_t dmReqData[4];
    for (int w = 0; w < 4; w++) dmReqData[w] = dut.io_dmem_req_wdata[w];

    if (getenv("EXTRACE") && dut.io_dbg_exValid &&
        (dut.io_dbg_exOp == 6 || dut.io_dbg_exOp == 34 || dut.io_dbg_exOp == 35))
      std::printf("E pc=0x%llx aIdx=%u a=0x%llx b=0x%llx aluA=0x%llx aluB=0x%llx\n",
                  (unsigned long long)dut.io_dbg_exPc, (unsigned)dut.io_dbg_exAIdx,
                  (unsigned long long)dut.io_dbg_exA, (unsigned long long)dut.io_dbg_exB,
                  (unsigned long long)dut.io_dbg_aluA, (unsigned long long)dut.io_dbg_aluB);

    dut.clock = 1;
    dut.eval();
    dut.clock = 0;
    dut.eval();

    if (getenv("TRACE") && dmReq)
      std::printf("M we=%d addr=0x%llx size=%u mask=0x%x data=0x%llx\n",
                  (int)dmReqWe, (unsigned long long)dmReqAddr, (unsigned)dut.io_dmem_req_size,
                  (unsigned)dmReqMask, (unsigned long long)dut.io_dmem_req_wdata[0]);

    imPend = imReq;
    imAddr = imReqAddr;
    ptPend = ptReq;
    ptAddr = ptReqAddr;
    dmPend = dmReq;
    dmAddr = dmReqAddr;
    dmWe = dmReqWe;
    dmMask = dmReqMask;
    dmSize = dut.io_dmem_req_size;
    for (int w = 0; w < 4; w++)
      for (int b = 0; b < 4; b++) dmData[4 * w + b] = (uint8_t)(dmReqData[w] >> (8 * b));

    if (getenv("TRACE") && dut.io_commit_valid)
      std::printf("C pc=0x%llx rd=%u we=%d data=0x%llx wf=%d flags=0x%llx op=%u rax=0x%llx rbx=0x%llx\n",
                  (unsigned long long)dut.io_commit_pc, (unsigned)dut.io_commit_rd,
                  (int)dut.io_commit_wreg, (unsigned long long)dut.io_commit_wdata,
                  (int)dut.io_commit_writesFlags, (unsigned long long)dut.io_commit_flags, (unsigned)dut.io_commit_op,
                  (unsigned long long)dut.io_dbg_gpr_0, (unsigned long long)dut.io_dbg_gpr_3);

    static bool trap_prev = false;
    static uint64_t trap_prev_pc = 0, trap_prev_vec = 0;
    if (diff && dut.io_dbg_trapValid) {
      uint64_t tpc = dut.io_dbg_trapPc, tvec = dut.io_dbg_trapVec;
      if (!trap_prev || tpc != trap_prev_pc || tvec != trap_prev_vec) {
        // The faulting instruction never retires; drop any stale pending
        // comparison and deliver the exception in the reference model.
        pend_valid = false;
        arch_flags = (arch_flags & ~(1ull << 9)); /* interrupt gate clears IF */
        difftest_raise_intr_ex(tvec, dut.io_dbg_trapErr, tpc);
      }
      trap_prev_pc = tpc; trap_prev_vec = tvec;
    }
    trap_prev = dut.io_dbg_trapValid;
    process_pending();
    bool new_commit = dut.io_commit_valid &&
                      (!commit_prev || dut.io_commit_pc != commit_prev_pc);
    commit_prev = dut.io_commit_valid;
    if (dut.io_commit_valid) commit_prev_pc = dut.io_commit_pc;
    if (new_commit) {
      if (dut.io_commit_writesFlags) arch_flags = dut.io_commit_flags;
      if (diff && dut.io_commit_op != 41 && !pend_valid) {
        pend_valid = true;
        pend_pc = dut.io_commit_pc;
        pend_flags = arch_flags;
        pend_op = dut.io_commit_op;
      }
    }
    if (dut.io_dbg_sysValid) {
      // CPUID/LIDT/IRET update architectural state without a commit trace.
      if (!dut.io_dbg_sysTrap) {
        arch_flags = dut.io_dbg_flags;
        if (diff)
          call_diff(dut.io_dbg_sysPc, arch_flags, dut.io_dbg_sysOp);
      }
    }
    if (getenv("TRAPTRACE"))
      std::fprintf(stderr, "[cyc %llu] trap=%d halted=%d fatal=%d rip=0x%llx\n",
                   (unsigned long long)cycle, (int)dut.io_dbg_trapValid,
                   (int)dut.io_halted, (int)dut.io_fatal,
                   (unsigned long long)dut.io_dbg_rip);
    if (getenv("TRAPTRACE") && dut.io_dbg_trapValid)
      std::fprintf(stderr, "[trap] valid vec=%u err=0x%llx pc=0x%llx\n",
                   (unsigned)dut.io_dbg_trapVec, (unsigned long long)dut.io_dbg_trapErr,
                   (unsigned long long)dut.io_dbg_trapPc);
    if (diff && dut.io_dbg_trapValid) {
      uint64_t pc = dut.io_dbg_trapPc;
      uint64_t vec = dut.io_dbg_trapVec;
      if (!trap_prev || pc != trap_prev_pc || vec != trap_prev_vec)
        difftest_raise_intr_ex(vec, dut.io_dbg_trapErr, pc);
      trap_prev_pc = pc; trap_prev_vec = vec;
    }
    trap_prev = dut.io_dbg_trapValid;
    halted = dut.io_halted;
    fatal = dut.io_fatal;
    if (halted || fatal) { cycle++; break; }
  }

  uint64_t gpr[16];
  gpr[0] = dut.io_dbg_gpr_0;
  gpr[1] = dut.io_dbg_gpr_1;
  gpr[2] = dut.io_dbg_gpr_2;
  gpr[3] = dut.io_dbg_gpr_3;
  gpr[4] = dut.io_dbg_gpr_4;
  gpr[5] = dut.io_dbg_gpr_5;
  gpr[6] = dut.io_dbg_gpr_6;
  gpr[7] = dut.io_dbg_gpr_7;
  gpr[8] = dut.io_dbg_gpr_8;
  gpr[9] = dut.io_dbg_gpr_9;
  gpr[10] = dut.io_dbg_gpr_10;
  gpr[11] = dut.io_dbg_gpr_11;
  gpr[12] = dut.io_dbg_gpr_12;
  gpr[13] = dut.io_dbg_gpr_13;
  gpr[14] = dut.io_dbg_gpr_14;
  gpr[15] = dut.io_dbg_gpr_15;

  if (const char *dump = getenv("DUMPMEM")) {
    std::ofstream f(dump, std::ios::binary);
    f.write((const char *)&mem[0x7f0000], 0x10000);
    f.write((const char *)&mem[0x600000], 0x10000);
  }

  uint32_t xw[16][4] = {{0}};
  for (int w = 0; w < 4; w++) xw[0][w] = dut.io_dbg_xmm_0[w];
  for (int w = 0; w < 4; w++) xw[1][w] = dut.io_dbg_xmm_1[w];
  for (int w = 0; w < 4; w++) xw[2][w] = dut.io_dbg_xmm_2[w];
  for (int w = 0; w < 4; w++) xw[3][w] = dut.io_dbg_xmm_3[w];
  for (int w = 0; w < 4; w++) xw[4][w] = dut.io_dbg_xmm_4[w];
  for (int w = 0; w < 4; w++) xw[5][w] = dut.io_dbg_xmm_5[w];
  for (int w = 0; w < 4; w++) xw[6][w] = dut.io_dbg_xmm_6[w];
  for (int w = 0; w < 4; w++) xw[7][w] = dut.io_dbg_xmm_7[w];
  for (int w = 0; w < 4; w++) xw[8][w] = dut.io_dbg_xmm_8[w];
  for (int w = 0; w < 4; w++) xw[9][w] = dut.io_dbg_xmm_9[w];
  for (int w = 0; w < 4; w++) xw[10][w] = dut.io_dbg_xmm_10[w];
  for (int w = 0; w < 4; w++) xw[11][w] = dut.io_dbg_xmm_11[w];
  for (int w = 0; w < 4; w++) xw[12][w] = dut.io_dbg_xmm_12[w];
  for (int w = 0; w < 4; w++) xw[13][w] = dut.io_dbg_xmm_13[w];
  for (int w = 0; w < 4; w++) xw[14][w] = dut.io_dbg_xmm_14[w];
  for (int w = 0; w < 4; w++) xw[15][w] = dut.io_dbg_xmm_15[w];

  process_pending();
  if (diff) difftest_finish();
  std::printf("{\"gpr\":[");
  for (int i = 0; i < 16; i++) std::printf("%s\"0x%016llx\"", i ? "," : "", (unsigned long long)gpr[i]);
  std::printf("],\"xmm\":[");
  for (int i = 0; i < 16; i++)
    std::printf("%s\"0x%08x%08x%08x%08x\"", i ? "," : "",
                xw[i][3], xw[i][2], xw[i][1], xw[i][0]);
  std::printf("],\"rip\":\"0x%016llx\",\"flags\":\"0x%016llx\",\"cycles\":%llu,"
              "\"memhash\":\"0x%016llx\",\"halted\":%d,\"fatal\":%d,\"exit\":%llu,"
              "\"fatalPc\":\"0x%016llx\",\"fatalCode\":%u}\n",
              (unsigned long long)dut.io_dbg_rip,
              (unsigned long long)dut.io_dbg_flags,
              (unsigned long long)dut.io_dbg_cycles,
              (unsigned long long)memhash(),
              halted ? 1 : 0, fatal ? 1 : 0,
              (unsigned long long)dut.io_exitCode,
              (unsigned long long)dut.io_fatalPc,
              (unsigned)dut.io_fatalCode);
  return fatal ? 3 : 0;
}
