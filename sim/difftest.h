#ifndef X86_DIFFTEST_H
#define X86_DIFFTEST_H

#include <cstddef>
#include <cstdint>

// DUT-side glue for a NEMU/XiangShan-style reference model loaded at runtime.
bool difftest_init(const char *ref_so, const uint8_t *image, size_t image_size,
                   uint64_t entry);
bool difftest_enabled();

// Called once per retired instruction, with the architectural post-state.
bool difftest_step(uint64_t pc, const uint64_t gpr[16],
                   const uint64_t xmm[16][2], uint64_t rflags, uint32_t op);

// Keep the reference memory coherent with DUT stores (mask bit i = byte i).
void difftest_store(uint64_t addr, const uint8_t *data, uint32_t mask);
bool difftest_check_load(uint64_t addr, const uint8_t *data, uint32_t mask);

void difftest_skip_next();
void difftest_finish();

#endif
