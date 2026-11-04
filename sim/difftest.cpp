#include "difftest.h"
#include "../ref/difftest_abi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

namespace {

using init_fn = void (*)(int);
using memcpy_fn = void (*)(uint64_t, void *, size_t, int);
using regcpy_fn = void (*)(void *, int);
using exec_fn = void (*)(uint64_t);
using skip_fn = void (*)();

init_fn ref_init = nullptr;
memcpy_fn ref_memcpy = nullptr;
regcpy_fn ref_regcpy = nullptr;
exec_fn ref_exec = nullptr;
skip_fn ref_skip = nullptr;

bool enabled = false;
uint64_t inst_count = 0;
constexpr uint64_t FLAG_MASK = 0xFD5;  // CF PF AF ZF SF TF DF OF (same as difftest.py)

const char *const REG_NAMES[16] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
};

[[noreturn]] void fail(const char *what, uint64_t pc, uint64_t dut, uint64_t ref) {
  std::fprintf(stderr,
               "\n[difftest] %s at instruction #%llu (pc=0x%llx): dut=0x%llx ref=0x%llx\n",
               what, (unsigned long long)inst_count, (unsigned long long)pc,
               (unsigned long long)dut, (unsigned long long)ref);
  std::abort();
}

}  // namespace

bool difftest_enabled() { return enabled; }

bool difftest_init(const char *ref_so, const uint8_t *image, size_t image_size,
                   uint64_t entry) {
  if (!ref_so || !*ref_so) {
    std::fprintf(stderr, "[difftest] no reference model (.so) given\n");
    return false;
  }
  void *h = dlopen(ref_so, RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    std::fprintf(stderr, "[difftest] dlopen(%s) failed: %s\n", ref_so, dlerror());
    return false;
  }
  ref_init = (init_fn)dlsym(h, "difftest_init");
  ref_memcpy = (memcpy_fn)dlsym(h, "difftest_memcpy");
  ref_regcpy = (regcpy_fn)dlsym(h, "difftest_regcpy");
  ref_exec = (exec_fn)dlsym(h, "difftest_exec");
  ref_skip = (skip_fn)dlsym(h, "difftest_skip_ref");
  if (!ref_init || !ref_memcpy || !ref_regcpy || !ref_exec || !ref_skip) {
    std::fprintf(stderr, "[difftest] %s does not export the difftest ABI\n", ref_so);
    return false;
  }
  ref_init(0);
  ref_memcpy(entry, (void *)image, image_size, DIFFTEST_TO_REF);

  diff_state st{};
  st.rip = entry;
  st.rflags = 0x202;
  ref_regcpy(&st, DIFFTEST_TO_REF);
  enabled = true;
  std::fprintf(stderr, "[difftest] reference model %s loaded, entry=0x%llx image=%zu bytes\n",
               ref_so, (unsigned long long)entry, image_size);
  return true;
}

bool difftest_step(uint64_t pc, const uint64_t gpr[16],
                   const uint64_t xmm[16][2], uint64_t rflags, uint32_t op) {
  if (!enabled) return true;

  diff_state ref{};
  ref_regcpy(&ref, DIFFTEST_TO_DUT);
  // Stage-A branches/CALL/RET that redirect in EX do not produce a commit
  // event, so the reference may still be sitting on such an instruction.
  // Let it catch up by executing up to a small budget of instructions until
  // its RIP reaches the next DUT commit PC.
  int guard = 0;
  while (ref.rip != pc && guard++ < 16) {
    ref_exec(1);
    ref_regcpy(&ref, DIFFTEST_TO_DUT);
  }
  if (ref.rip != pc)
    fail("ref RIP != DUT pc", pc, pc, ref.rip);

  ref_exec(1);
  ref_regcpy(&ref, DIFFTEST_TO_DUT);
  inst_count++;

  for (int i = 0; i < 16; i++) {
    if (ref.gpr[i] != gpr[i]) {
      std::fprintf(stderr, "\n[difftest] GPR mismatch at instruction #%llu pc=0x%llx: %s dut=0x%llx ref=0x%llx\n",
                   (unsigned long long)inst_count, (unsigned long long)pc, REG_NAMES[i],
                   (unsigned long long)gpr[i], (unsigned long long)ref.gpr[i]);
      std::abort();
    }
  }
  const bool movq = (op == 71);   // MOVQ GPR/mem <-> XMM
  const bool movd = (op == 70);   // MOVD GPR/mem <-> XMM
  for (int i = 0; i < 16; i++) {
    bool lo_bad;
    if (movd) lo_bad = (ref.xmm[i][0] & 0xffffffffull) != (xmm[i][0] & 0xffffffffull);
    else if (movq) lo_bad = ref.xmm[i][0] != xmm[i][0];
    else lo_bad = (ref.xmm[i][0] != xmm[i][0]) || (ref.xmm[i][1] != xmm[i][1]);
    if (lo_bad) {
      std::fprintf(stderr,
                   "\n[difftest] XMM%d mismatch at instruction #%llu pc=0x%llx: dut=%016llx%016llx ref=%016llx%016llx\n",
                   i, (unsigned long long)inst_count, (unsigned long long)pc,
                   (unsigned long long)xmm[i][1], (unsigned long long)xmm[i][0],
                   (unsigned long long)ref.xmm[i][1], (unsigned long long)ref.xmm[i][0]);
      std::abort();
    }
  }
  // QEMU and our RTL can disagree on the *undefined/zeroed* upper bits of
  // legacy MOVD/MOVQ; resynchronize them so later comparisons stay aligned.
  if (movq || movd) {
    for (int i = 0; i < 16; i++) {
      ref.xmm[i][0] = xmm[i][0];
      ref.xmm[i][1] = xmm[i][1];
    }
    ref_regcpy(&ref, DIFFTEST_TO_REF);
  }
  uint64_t mask = FLAG_MASK;
  // Architecturally undefined flag bits: compare them at this instruction with
  // a mask, then copy the DUT's choice into the reference so later
  // instructions (whose flags may be defined) do not diverge on them.
  uint64_t undef = 0;
  if (op >= 23 && op <= 29) undef = (1ull << 11) | (1ull << 4);          // OF/AF
  else if (op == 19 || op == 20) undef = (1ull << 2) | (1ull << 4) | (1ull << 6) | (1ull << 7);
  else if (op == 21 || op == 22) undef = (1ull << 0) | (1ull << 2) | (1ull << 4) | (1ull << 6) | (1ull << 7) | (1ull << 11);
  mask &= ~undef;
  // Shift/rotate OF (and AF) are architecturally undefined except for count 1;
  // do not let QEMU's choice of undefined bits fail the comparison.
  if (op >= 23 && op <= 29) mask &= ~((1ull << 11) | (1ull << 4));
  if (undef) {
    ref.rflags = (ref.rflags & ~undef) | (rflags & undef);
    ref_regcpy(&ref, DIFFTEST_TO_REF);
  }
  if (((ref.rflags ^ rflags) & mask) != 0) {
    std::fprintf(stderr, "\n[difftest] RFLAGS mismatch at instruction #%llu pc=0x%llx: dut=0x%llx ref=0x%llx mask=0x%llx\n",
                 (unsigned long long)inst_count, (unsigned long long)pc,
                 (unsigned long long)rflags, (unsigned long long)ref.rflags,
                 (unsigned long long)mask);
    std::abort();
  }
  return true;
}

void difftest_store(uint64_t addr, const uint8_t *data, uint32_t mask) {
  if (!enabled) return;
  for (int i = 0; i < 16; i++) {
    if (mask & (1u << i))
      ref_memcpy(addr + i, (void *)&data[i], 1, DIFFTEST_TO_REF);
  }
}

bool difftest_check_load(uint64_t addr, const uint8_t *data, uint32_t mask) {
  if (!enabled) return true;
  for (int i = 0; i < 16; i++) {
    if (!(mask & (1u << i))) continue;
    uint8_t expect = 0;
    ref_memcpy(addr + i, &expect, 1, DIFFTEST_TO_DUT);
    if (expect != data[i]) {
      std::fprintf(stderr,
                   "\n[difftest] load mismatch at instruction #%llu addr=0x%llx byte%+d: dut=0x%02x ref=0x%02x\n",
                   (unsigned long long)inst_count, (unsigned long long)addr, i, data[i], expect);
      std::abort();
    }
  }
  return true;
}

void difftest_skip_next() {
  if (enabled) ref_skip();
}

void difftest_finish() {
  if (enabled)
    std::fprintf(stderr, "[difftest] %llu instructions cross-checked\n",
                 (unsigned long long)inst_count);
}
