# emu 逐指令测试计划（v1）

本文定义基于 `build/emu`（Verilator 模型 + NEMU 风格 `.so` 参考模型）的完整
测试计划，并记录本轮测试发现与修复的问题。

## 1. 目标

1. 用**逐指令对拍**覆盖 Stage-A 已实现的全部用户态 ISA 子集。
2. 用**随机程序**扩大组合覆盖，尽早暴露边界语义错误。
3. 明确未定义行为（flags、MOVD/MOVQ 高位）的处理策略，避免假失败。
4. 为后续系统态（分页/异常）与 Stage-B 逐退休对拍预留接口。

## 2. 测试分层

| 层 | 内容 | 命令 | 当前结果 |
| --- | --- | --- | --- |
| L0 | 解码向量（编码/长度/合法性） | `make decode-test` | 693/693 |
| L1 | 定向用户态用例，逐指令对拍 | `make emu-test` | 37/37 |
| L2 | 随机程序 fuzz（确定性种子） | `make emu-fuzz` | 200/200（累计 360/360） |
| L3 | 系统态（分页/IDT/异常） | `make sys-test` | 10/10（暂由结束态对拍覆盖） |
| L4 | Stage-B 逐退休对拍 | 规划中 | — |
| L5 | 单元测试 | `sbt test` | 21/21 |

## 3. emu 对拍规则

每条退休指令比较：

- 16 个 GPR
- 16 个 XMM（MOVD/MOVQ 的高位按 QEMU/Intel 差异做低位比较并重同步）
- RFLAGS（掩码 `0xFD5`）
- PC 对齐（参考机 RIP 必须等于 DUT 提交 PC）
- 内存：DUT load 与参考内存按字节比较；参考机自行执行 store

未定义/弱定义行为处理：

| 场景 | 处理 |
| --- | --- |
| 移位/旋转计数 > 1 的 OF/AF | 该指令比较时掩码，随后把 DUT 选择写回参考机 |
| MUL/IMUL 的 SF/ZF/AF/PF | 同上 |
| DIV/IDIV 的全部算术 flags | 同上 |
| 移位计数为 0 | x86 规定 flags 不变；RTL 已按此实现 |
| MOVD/MOVQ 的 XMM 高位 | 只比较低位，随后用 DUT 值同步参考机 |
| HLT / MMIO | 不进入参考模型 |
| 在 EX 重定向但不 commit 的分支/CALL/RET | 参考机 catch-up 执行 |

## 4. 随机程序生成器

`tools/emu_fuzz.py`：

- 固定栈（0x800000）、数据基址（r14=0x600000）、r15 用于停机
- 随机组合：立即数/寄存器 MOV、ADD/SUB/AND/OR/XOR/ADC/SBB/IMUL、
  INC/DEC/NEG/NOT、SHL/SHR/SAR/ROL/ROR（立即数与 CL）、
  内存读写（8/4/2 字节）、PUSH/POP、CMP/TEST+SETcc、
  SSE（MOVQ/MOVDQA/PADDD/PSUBD/PXOR/PAND/POR/内存 MOVDQA）
- 每个种子生成 20–60 条指令，程序以写 0xF0000000 停机
- 失败程序保存到 `tests/fuzz_fail/`，便于复现

## 5. 本轮测试发现并修复的问题

1. **部分写转发**：`movw/movb` 后转发返回未合并结果，丢高位。
2. **SAHF 误写 GPR**：SAHF 只应改 flags。
3. **0xFE 组未强制 8 位**：`incb %al` 按 32 位算 flags。
4. **ROL/ROR 改写 ZF/SF/PF**：旋转只定义 CF（count=1 时还有 OF）。
5. **MUL/IMUL 定义未定义 flags**：SF/ZF/AF/PF 应为未定义。
6. **PUSH/POP 操作数宽度**：`pop` 按 32 位 load，丢高 32 位。
7. **MOVD/MOVQ 高位**：Stage-A 标量合并保留旧高位，应按 Intel 规则清零。
8. **移位计数为 0**：应完全不改 flags，原实现会清 CF。
9. **QNaN indefinite 符号**：x86 无效运算应产生负 qNaN（`0xFFF8...`/`0xFFC00000`）。

其中 1–6 由定向用例发现，7–8 由 fuzz（种子 24、41）发现。

## 6. 覆盖矩阵（用户态）

| 类别 | 代表用例 / fuzz 操作 |
| --- | --- |
| 整数 ALU/flags | t1/t5/t8、fuzz ALU+SETcc |
| 部分宽度 | t5、fuzz movb/movw/movl |
| 移位/旋转 | t1、fuzz 立即数/CL 移位 |
| 乘除/CDQ | t17/t18、t36/t37 |
| 栈/CALL/RET | t10/t11、fuzz push/pop |
| 内存/RMW/XCHG/XADD | t6/t24–t29 |
| 间接控制流 | t21/t22/t27 |
| SSE2 整数/浮点 | t13–t21、fuzz SSE |
| 分页/异常 | sys-test（emu 暂不覆盖） |

## 7. 后续计划

1. **P1 系统态**：DUT 暴露 trap 元数据（vector/error/epc），参考模型实现
   `raise_intr` 与分页 walk，使 `emu --diff` 覆盖 t25/t26/t27/t31。
2. **P2 Stage-B**：给 OoO TB 增加退休事件端口，按退休顺序逐条调用同一 `.so`。
3. **P3 扩展 fuzz**：加入浮点 SSE、条件分支、更多内存寻址模式与异常注入。
4. **P4 CI**：`make emu-test emu-fuzz sys-test regress ooo-test` 一条命令。
