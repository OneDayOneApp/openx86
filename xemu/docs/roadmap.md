# XEMU 开发计划 v2（对齐 NEMU 功能，Unicorn 作为指令验证标准）

## 1. 目标与基线

把 XEMU 从“能通过当前测试的参考模型”发展为 **NEMU 级的 x86-64 模拟器**：

- 指令语义以 **Unicorn（QEMU）** 为逐指令验证标准
- 工具链对齐 NEMU：monitor、itrace/mtrace/ftrace、watchpoint、snapshot、设备、批处理
- 系统模式（分页/异常/权限/中断/设备）以 RTL co-sim + 定向测试为准
- 保持 `difftest_*` ABI 与 `build/emu` 兼容

当前基线（v0.2.1）：

| 项 | 状态 |
| --- | --- |
| 解码 | 693 向量，长度 + 字段级对齐 |
| 整数/SSE2 子集 | 37/37 定向；1000/1000 fuzz |
| 系统模式 | lockstep 10/10；co-sim 10/10 |
| CLI | `build/xemu`：batch/monitor/itrace/ring buffer |
| 参考模型 | `libxemu.so` 默认，Unicorn 可选 |

## 2. NEMU 功能对照

| NEMU 功能 | XEMU 现状 | 计划 |
| --- | --- | --- |
| ISA 执行 | x86-64 子集 | P2 扩展到 NEMU 级子集 |
| monitor | `si/info r/x/q` | P4 增加 `p`、watchpoint、`info w` |
| itrace | `--trace` | P4 增加反汇编/操作数 |
| mtrace | 无 | P4 增加内存访问 trace |
| ftrace | 无 | P4 增加函数调用 trace |
| snapshot | 无 | P4 增加 save/load |
| difftest | 完整 ABI | 保持 |
| 设备 | 仅 halt MMIO | P5 serial/timer/keyboard/VGA |
| 中断 | 仅异常 | P3 外部 IRQ + timer |
| 批处理/日志/统计 | `-b`/`--trace` | P4 `-l`、`-d`、统计 |
| 表达式求值 | 无 | P4 `p EXPR` |
| 性能统计 | 无 | P6 IPC/指令计数 |

## 3. 指令验证标准：Unicorn

### 3.1 适用范围

Unicorn 用于**用户态、paging 关闭**的指令语义验证：

- GPR / RFLAGS / XMM / 内存副作用 / RIP
- 编码宽度、partial write、RIP 相对、ModRM/SIB

Unicorn 不覆盖：分页、IDT、异常投递、CPL、设备、外部中断。
这些由 **RTL co-sim + 定向测试 + 体系结构规范** 验证。

### 3.2 逐指令对拍框架

新增 `tools/isa_diff.py`：

1. 用 llvm-mc 汇编单条指令模板，或直接编码
2. 双方初始化相同状态（GPR/RFLAGS/XMM/内存）
3. 在 `0x400000` 执行**恰好一条**指令
4. 比较：
   - RIP、GPR×16、XMM×16、内存写入
   - RFLAGS：只比较**已定义位**；未定义位记录并同步
5. 失败向量保存到 `tests/isa_fail/`，可复现

### 3.3 已知语义差异（白名单）

| 差异 | 处理 |
| --- | --- |
| 移位/旋转/MUL/DIV 未定义 flags | 掩码后比较，记录参考值 |
| 逻辑运算 AF | 未定义，掩码 |
| MOVD/MOVQ XMM 高位 | QEMU 与 RTL 可能不同，比较低位并重同步 |
| NaN payload / 负 qNaN | 规范化后比较；记录 payload 差异 |
| 非规范/超范围 CVT | 以 Unicorn 结果为准，标注 undefined |
| 系统指令 | 不走 Unicorn，走 RTL |

### 3.4 覆盖要求

- 对**所有已实现 op**：至少覆盖 reg/reg、reg/mem、mem/reg、imm 形式
- 位宽 8/16/32/64、REX.W、66/F2/F3 前缀
- 每种指令 ≥ 20 个随机操作数向量；边界值（0、-1、符号位、进位/借位）
- CI 门槛：**已实现指令 100% 有 Unicorn 对拍向量**，0 mismatch

## 4. 阶段计划

### P1 — Unicorn 指令验证框架（已完成初版）

- 实现 `tools/isa_diff.py`、失败归档、覆盖率报告
- 覆盖现有整数/SSE2 子集，补齐边界向量
- 验收：现有 op 100% 覆盖、≥5000 向量、0 mismatch；CI 脚本
- 当前：`make isa-diff` → **7100/7100**（71 模板 × 100 随机状态）

### P2 — ISA 扩展（进行中）

按 Unicorn 对拍驱动实现：

- 字符串：MOVS/STOS/LODS/SCAS/CMPS + REP
- 控制流：内存间接 CALL/JMP（`FF /2`、`FF /4`）
- 位操作：BSF/BSR、BT/BTS/BTR/BTC、POPCNT（如纳入）
- 原子：CMPXCHG、CMPXCHG8B/16B
- SSE2 扩展：PADD/PSUB 全宽度、PCMPEQ/PCMPGT、PSHUF、PUNPCK、
  PMOVMSKB、MASKMOVDQU、MOVNT、更多 CVT
- 进度：字符串 + REP、BSF/BSR、BT/BTS/BTR/BTC、CMPXCHG/8B/16B、SSE2 整数(PADD/PSUB 全宽度、PCMPEQ/GT、PSHUFD、PUNPCK、PMOVMSKB、MOVNTDQ)、间接 CALL/JMP 已完成，Unicorn 向量 14300/14300
- 验收：每类指令 Unicorn 向量全过；fuzz 覆盖新 op

### P3 — 系统模式与中断（XEMU 侧已完成初版）

- CPL0/3、TSS、权限检查
- 中断门/陷阱门、外部 IRQ 注入
- timer 中断、串口输入中断
- `SYSCALL/SYSRET` 权限切换
- 进度：LGDT/LIDT/LTR、CPL0/3 换栈、IDT 门属性/DPL、外部 IRQ、timer MMIO、
  SYSCALL/SYSRET 已实现；`make xemu-sys-test` 2/2（t40_cpl3、t41_timer）
- 说明：RTL 尚未实现 CPL3/TSS/中断注入，因此这两项用 XEMU 自身断言语义
- 验收：RTL co-sim + 中断定向测试；Unicorn 不参与

### P4 — NEMU 工具链（2–3 周）

- monitor：`p EXPR`、watchpoint、`info w`
- mtrace/ftrace、itrace 反汇编
- snapshot：`save/load` 到文件
- 日志/统计：`-l`、`-d`、指令/周期统计
- 验收：交互式 demo + 文档

### P5 — 设备（2 周）

- serial（输出/输入）、timer/RTC、keyboard、VGA framebuffer
- MMIO 设备框架与中断连线
- 验收：hello world / 简单 OS 启动 demo

### P6 — 性能与发布（1 周）

- 性能剖析，目标 ≥10 MIPS
- fuzz ≥10000 种子、CI 全量
- 发布 v1.0

## 5. 验收矩阵

| 类别 | Unicorn | RTL lockstep | 定向 | fuzz |
| --- | --- | --- | --- | --- |
| 整数/SSE2 | ✅ 主标准 | ✅ | ✅ | ✅ |
| 字符串/原子 | ✅ | ✅ | ✅ | 新增 |
| 分页/异常/IDT | ❌ | ✅ | ✅ | 受限 |
| 权限/中断/设备 | ❌ | ✅ co-sim | ✅ | 受限 |
| 工具链 | — | — | demo | — |

## 6. 风险

| 风险 | 应对 |
| --- | --- |
| QEMU 未定义行为与 RTL 不同 | 白名单 + 掩码；RTL 为系统模式最终标准 |
| Unicorn 不支持 system mode | 明确分层；P3 用 RTL |
| 指令范围膨胀 | 按 NEMU 级子集列清单，不做全 x86 |
| 设备/OS 依赖复杂 | 先 serial+timer，逐步加 |
| 性能 | 先正确性后优化；profile 驱动 |

## 7. 立即可执行（P1 Sprint）

1. `tools/isa_diff.py`：单指令 Unicorn vs XEMU 对拍
2. 为 ALU/MOV/移位/乘除/SSE2 生成 ≥5000 向量
3. 修复首批 mismatch，建立 `tests/isa_fail/`
4. `make isa-diff` + CI
5. 输出覆盖率报告（按 op 统计）
