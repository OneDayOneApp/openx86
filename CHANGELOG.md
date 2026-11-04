# Changelog

## v0.2.1 — 系统模式逐指令 lockstep

### 修复

- **t27_iret**：UD2 陷阱改在 EX 阶段触发；sys 微码（trap/LIDT/IRET/CPUID）期间冻结流水线，
  避免 UD2 之后的指令投机退休并污染陷阱帧 RFLAGS
- CPUID 结果写入延后到微码 step2/3，避免前一条指令的快照竞争
- 新增 `make sys-lockstep-test`：系统测试逐指令对拍 **10/10**

### 验证

| 套件 | 结果 |
| --- | --- |
| 系统模式 lockstep | **10/10** |
| 系统模式 co-sim | 10/10 |
| 用户态逐指令 | 37/37 |
| Stage-A vs Unicorn | 37/37 |
| Stage-B vs Stage-A | 29/29 |
| 随机 fuzz（200 种子） | 200/200 |

## v0.2.0 — 自研 XEMU 参考模型

### 新增

- **XEMU**：C11 自研 x86-64 参考模拟器，无 Unicorn 依赖
  - 表驱动解码（693 条向量、字段级对齐 RTL）
  - 整数执行、flags、部分写、栈/控制流
  - SSE2 子集（XMM 传送、整数 SIMD、SS/ SD/PS/PD 浮点、CVT）
  - 4 级分页、#PF/#UD/#DE/#GP、IDT/IRETQ、SYSCALL/SYSRET、CPUID、MSR
- `build/emu` **默认参考模型**切换为 `build/ref/libxemu.so`
- Unicorn 参考模型保留：`make ref-unicorn` + `--ref-so=build/ref/libx86ref.so`
- 新增 `make xemu-regress`、`make xemu-decode-test`、`make sys-diff-test`
- `tools/xemu_sysrun.py`：DUT + XEMU 系统模式 co-sim

### 验证

| 套件 | 结果 |
| --- | --- |
| XEMU 解码（长度 + 字段） | 693/693 + 693/693 |
| XEMU 定向用户态 | 37/37 |
| XEMU 系统模式 co-sim | 10/10 |
| 随机 fuzz（XEMU，逐指令） | **1000/1000** |
| Stage-A vs Unicorn | 37/37 |
| Stage-B vs Stage-A | 29/29 |

### 已知限制

- 系统模式陷阱采用最终状态 co-sim，逐指令 trap 对拍待实现
- SSE2 仅覆盖当前测试/fuzz 子集
- 字符串指令、内存间接 CALL 未实现

## v0.1.0 — 首个公开版本

### 新增

- **Stage-A 顺序核**：变长 x86-64 前端、五级流水、整数 + SSE2 子集、
  4 级分页、异常/IDT/IRETQ、SYSCALL/SYSRET、CPUID、INVLPG、MSR 子集。
- **Stage-B 乱序核**：重命名（GPR/flags/XMM）、ROB 32、IQ 8、PRF、
  StoreBuffer、双发射端口 + 2 宽提交、数据/取指分页。
- **验证体系**：Unicorn 结束态对拍、Stage-B vs Stage-A 对拍、系统态测试、
  NEMU 风格逐指令 DiffTest（`build/emu` + `libx86ref.so`）。
- **随机测试**：`tools/emu_fuzz.py`，固定种子、失败用例落盘。
- **文档**：微架构手册、Stage-A、验证手册、emu 测试计划、XEMU 研发计划。

### 验证结果

| 套件 | 结果 |
| --- | --- |
| 解码向量 | 693 / 693 |
| Stage-A vs Unicorn | 37 / 37 |
| 系统测试 | 10 / 10 |
| Stage-B vs Stage-A | 29 / 29 |
| emu 逐指令对拍（GPR/XMM/RFLAGS/内存） | 37 / 37 |
| 随机 fuzz 逐指令对拍 | 200/批，累计 360/360 |
| Scala 单测 | 21 / 21 |

### 已知限制

- 前端仍为 1 宽译码/派发；ROB/IQ/PRF 规模小于昆明湖 V3。
- Stage-B 尚无精确异常/IDT。
- 字符串指令、内存间接 CALL 未实现。
- `emu --diff` 基于 Unicorn，仅覆盖 paging 关闭的用户态用例。
- 尚未加入 LICENSE 文件（待定）。
