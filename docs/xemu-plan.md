# XEMU 研发计划（x86-64 参考模拟器）

## 1. 背景

当前 `emu` 的参考模型 `libx86ref.so` 内部使用 **Unicorn**：ABI 和逐指令对拍链路已经跑通，
但 Unicorn 偏 user-mode，不模拟 guest 分页与 IDT 异常，因此 `emu --diff` 只能覆盖
paging 关闭的用户态用例，系统测试仍依赖结束态对拍。

**XEMU** 的目标是用 C11 手写一个 system-mode x86-64 参考模拟器，替换 Unicorn：

- 导出与 NEMU/XiangShan 相同的 `difftest_*` ABI（DUT 侧 glue 不改）
- 支持 paging、异常/IDT/IRETQ，使 `emu --diff` 能覆盖系统测试
- 无第三方 ISA 依赖、可移植、确定性、可控的未定义行为策略
- 只实现本项目已实现并测试过的子集，不追求完整 x86

## 2. 范围

### In scope（v1）

- 64 位长模式、整数指令子集（与当前 Stage-A 对齐）
- SSE2 子集：XMM 传送、整数 SIMD、标量/打包浮点算术、比较、转换
- 4 级分页（4KB 页）、CR0/2/3/4、TLB（可选）
- 异常：#PF/#UD/#DE/#GP、IDT 门、错误码、IRETQ、LIDT
- SYSCALL/SYSRET、CPUID、INVLPG、MSR 子集（测试所需）
- MMIO 停机（0xF0000000）
- NEMU 风格 difftest ABI + 可选的独立 CLI

### Out of scope（v1 不做）

- 16/32 位模式、段式内存、任务切换、VMX/SVM
- x87、AVX/AVX2、FMA、AES、字符串指令（按后续需求再加）
- 中断控制器/设备、SMP、缓存一致性

## 3. 总体架构

```
xemu/
  difftest.c      # difftest_* ABI 导出、状态快照
  state.c/.h      # gpr[16], rip, rflags, xmm[16], cr/idtr/msr
  mem.c/.h        # 16MB 物理内存 + MMIO + 读写/取指
  decode.c/.h     # 前缀/REX/ModRM/SIB/disp/imm → DecodedInst
  exec.c          # 整数/控制流/栈/系统指令
  sse.c           # XMM/整数 SIMD/浮点（软浮点）
  mmu.c           # 4 级页表 walk、权限、#PF 错误码
  trap.c          # 异常投递、IDT、IRETQ
  main.c          # 可选：独立运行镜像
```

关键数据结构：

```c
typedef struct {
  uint64_t gpr[16];
  uint64_t rip, rflags;
  uint64_t xmm[16][2];
  uint64_t cr0, cr2, cr3, cr4;
  struct { uint64_t base; uint16_t limit; } idtr;
  uint64_t efer, star, lstar, sfmask;
  uint64_t fs_base, gs_base;
  int      halted;
} XemuState;

typedef struct {
  uint8_t  len;
  uint16_t op;          // Ops 编号（与 RTL 对齐）
  uint8_t  opsz;        // 1/2/4/8
  uint8_t  simd;        // 标量/打包
  uint8_t  a_kind, b_kind;
  uint8_t  a_idx, b_idx;
  uint64_t imm;
  uint64_t mem_off;     // disp
  uint8_t  mem_base, mem_index, mem_scale;
  bool     rip_rel;
} DecodedInst;
```

## 4. 关键设计决策

1. **解码与 RTL 对齐**：`DecodedInst` 的操作数约定与 `Decoder.scala` 保持一致
   （dir/a/b、memsz、simd），便于复用 693 条解码向量做单元测试。
2. **flags 策略**：按 Intel 规则更新；未定义位（移位 OF/AF、MUL/IMUL 的
   SF/ZF/AF/PF、DIV 的全部算术位）**保持旧值**，difftest 侧继续用掩码/同步策略。
3. **浮点**：移植 `FpUnit.scala` 的语义到 C（整数软浮点，RNE，FTZ 输入，
   QNaN indefinite = 0xFFF8…/0xFFC00000），不依赖 host FPU 舍入模式。
4. **分页**：实现 walk、U/S/W/NX、PS 大页（可选）、A/D 位（写时置位；v1 可只读检查）。
5. **异常**：统一 `raise_exc(vector, error_code)`；`difftest_raise_intr(NO)` 走同一路径，
   与 DUT 的 trap 投递对齐；faulting 指令由 DUT 调用 `difftest_skip_ref()` 跳过。
6. **ABI 不变**：`difftest_init/memcpy/regcpy/exec/skip_ref/raise_intr` 名称与语义
   与 NEMU/XiangShan 一致；DUT 侧只改 `--ref-so` 路径。

## 5. 里程碑与验收

### M0 骨架与 ABI（已完成）

- `xemu/` 目录、Makefile `xemu-so` → `build/ref/libxemu.so`
- 实现 init/memcpy/regcpy/exec/skip_ref
- 最小指令：`MOV r64, imm64`、`HLT`
- 验收：`tests/emu/xemu_m0.S` 逐指令对拍通过（`make xemu-test`）

### M1 解码器（已完成）

- 前缀、REX、ModRM/SIB、disp/imm、RIP 相对
- 表驱动 primary/two-byte 解码，覆盖当前 Ops 子集
- 单元测试：复用 693 条解码向量；长度/合法性 + 字段级（op/simd/opsz/memsz/操作数/imm/base/index/disp）对拍
- 验收：693/693（长度与字段均通过，`make xemu-decode-test`）

### M2 整数执行（整数部分已完成；SSE 归入 M4）

- MOV/MOVZX/MOVSX/MOVSXD/LEA、ALU、移位/旋转、INC/DEC/NEG/NOT、MUL/IMUL/DIV/IDIV、
  CDQ/CQO/CBW、XCHG/XADD、CMP/TEST/SETcc/CMOVcc
- 部分宽度（1/2/4/8）、flags 语义、内存读写、PUSH/POP/CALL/RET、JMP/JCC/LOOP*
- 状态：`make xemu-regress` **37/37**（整数 + SSE2 子集）、XEMU fuzz 200/200
- 验收：`make emu-test` 37/37（ref-so 指向 XEMU）+ fuzz 200

### M3 分页/异常（已完成，系统模式）

- CR0/2/3/4、4 级 walk、#PF 错误码、IDT、LIDT、IRETQ
- #UD/#DE/#GP、SYSCALL/SYSRET、CPUID、INVLPG、MSR 子集
- 与 DUT trap 元数据/`raise_intr` 对接
- 状态：`make sys-diff-test` 10/10（DUT + XEMU 同镜像 co-sim，比较最终 GPR/RIP/RFLAGS）
- 说明：逐指令 trap 对拍仍需 RTL 提交事件配合；当前 M3 验收采用系统模式结束态对比
- 进展：已暴露 sysop 完成事件并实现逐指令 lockstep，9/10 通过；t27 的 RTL 陷阱栈 RFLAGS 槽为 0x97（非 0x246），需 RTL 修复后对齐

### M4 SSE2 子集（已完成）

- MOVD/MOVQ/MOVDQA/MOVUPS/MOVSS/MOVSD
- PADDD/PSUBD/PAND/POR/PXOR
- ADD/SUB/MUL/DIV/SQRT/MIN/MAX（标量+打包）、COMIS/UCOMIS、CVT 系列
- 验收：SSE 定向用例 37/37、fuzz 200/200；NaN/部分写语义与 RTL 一致

### M5 集成与替换（已完成）

- `build/ref/libxemu.so` 替换 Unicorn 版
- `emu-test`、`emu-fuzz`（≥1000 种子）、`sys-test` 全过
- 性能目标：≥ 5 MIPS（本机单线程）
- 状态：`REF_SO` 默认 `libxemu.so`；`make emu-test` 37/37；fuzz **1000/1000**；`make sys-diff-test` 10/10
- 验收：CI 脚本一条命令

### M6 发布（2–3 天）

- 文档、许可证头、README、release tag
- 可选：独立 `xemu -i image` CLI

## 6. 测试策略

| 阶段 | 方法 |
| --- | --- |
| 开发期 | XEMU vs Unicorn：同一镜像逐指令比较，快速定位 |
| 集成期 | XEMU vs RTL：`emu --diff --ref-so=libxemu.so` |
| 随机 | fuzz（种子固定），失败保存到 `tests/fuzz_fail/` |
| 定向 | 现有 37 个 asm + 10 个 system |
| 解码 | 693 条向量与 RTL 解码器对比 |
| 回归 | Stage-A 37、系统 10、OoO 29、emu 37、fuzz 200 |

## 7. 风险与应对

| 风险 | 应对 |
| --- | --- |
| flags 未定义位导致假失败 | 明确“保持旧值”策略，difftest 掩码/同步 |
| 浮点位级不一致 | 软浮点移植 FpUnit，NaN indefinite 固定；重点用例对拍 |
| 分页 A/D/保留位细节 | 先实现测试所需最小语义，再按规范补齐 |
| trap 协议复杂 | 先打通 `raise_intr` + skip_ref，再覆盖 #PF/#UD |
| 工作量 | 只做子集；里程碑独立可验收；每阶段都保持 emu 可用 |

## 8. 任务拆分（Issue 建议）

1. XEMU M0：骨架 + ABI + Makefile
2. XEMU M1：解码器 + 693 向量
3. XEMU M2：整数执行 + emu 37/37
4. XEMU M3：分页/异常 + sys-test
5. XEMU M4：SSE2
6. XEMU M5：替换 Unicorn + fuzz 1000
7. XEMU M6：文档/发布

## 9. 验收总标准

- `build/emu -i <image>` 默认参考模型可切换为 `libxemu.so`
- 定向 37/37、系统 10/10、随机 fuzz ≥1000 全过
- 无 Unicorn 依赖（仅测试期可选用）
- 文档记录 ABI、flags 策略、异常/分页语义与已知限制
