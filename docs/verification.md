# 验证与现状（v3.8）

本文汇总 2026 年 11 月当前实现的全部验证结果、覆盖范围、性能观察、已知限制与后续路线。
所有数字均由仓库内脚本在本机重新跑出，命令见文末。

## 1. 测试矩阵

| 层级 | 内容 | 脚本 | 数量 | 结果 |
| --- | --- | --- | --- | --- |
| L0 单元 | Chisel/Scala 单元测试（ALU/FpUnit/Decoder 等） | `sbt test` | 21 | 21/21 |
| L1 解码 | 解码向量（前缀/ModRM/SIB/立即数/SSE/系统） | `make decode-test` | 693 | 693/693 |
| L2 顺序核 | Stage-A vs Unicorn 结束状态对拍 | `make regress` | 37 | 37/37 |
| L2.5 系统 | 分页/异常/IDT/IRETQ/SYSCALL/CPUID/INVLPG/取指缺页 | `make sys-test` | 10 | 10/10 |
| L3 乱序核 | Stage-B vs Stage-A（GPR/RIP/RFLAGS/XMM/内存哈希） | `python3 tools/ooo_regress.py` | 29 | 29/29 |
| L2.6 逐指令 | 香山式 `emu --diff` vs `libx86ref.so`（NEMU ABI） | `make emu-test` | 37 | 37/37 |
| L2.7 随机 | 随机程序逐指令对拍（确定性种子） | `make emu-fuzz` | 200/批 | 累计 360/360 |

Stage-A 与 Stage-B 使用同一份 x86 汇编，二进制从 `0x400000` 加载，栈在 `0x800000`，`0xF0000000` 为停机 MMIO。

## 2. 覆盖范围

### Stage-A（顺序五级：取指/译码/EX/MEM/WB）

- 变长解码：REX、ModRM/SIB、disp/imm、RIP 相对、2/3 字节 opcode、SSE 前缀
- 整数：MOV/MOVZX/MOVSX/MOVSXD/LEA、ADD/ADC/SUB/SBB/AND/OR/XOR/CMP/TEST、
  INC/DEC/NEG/NOT、移位/循环、MUL/IMUL/DIV/IDIV、CDQ/CQO/CBW、XCHG、XADD
- 控制流：JMP/JCC/LOOP/LOOPNE/LOOPE/JRCXZ、直接与寄存器/内存间接 CALL/RET
- 栈：PUSH/POP/PUSHF/POPF、CALL/RET
- 访存：MOV 全宽度、SSE MOV、ALU 内存源与内存目的（RMW）、XCHG/XADD、COMIS 内存源
- SSE2 浮点子集：加法/减法/乘法/除法/开方/最值、比较、整数↔浮点转换、A/D 逻辑、MOVD/MOVQ
- 系统：CR0/2/3/4、MSR（EFER/STAR/LSTAR/SFMASK）、SYSCALL/SYSRET、CPUID、INVLPG、
  LIDT、IDT 异常投递、IRETQ、INT n/INT3、HLT
- 分页：4 级页表、32 项 TLB、取指与数据翻译、#PF 错误码

### Stage-B（乱序：重命名/ROB/IQ/PRF）

- 1 宽取指/译码/派发；GPR 64、flags 16、XMM 32 物理寄存器；ROB 32；IQ 8
- 每周期最多 2 条发射（第二端口）与 2 条提交
- 分支检查点 + 误预测冲刷；RET/间接 CALL 重定向
- MQ 8、StoreBuffer 8、顺序提交、store 提交时写回
- 数据/取指各一个 MMU（4 级 walk + PTW 端口），页表写从 StoreBuffer 转发
- ISA 覆盖与 Stage-A 对齐（含 RMW、XCHG、XADD、COMIS 内存源、间接控制流、MUL/DIV、CDQ/CBW）

## 3. 性能观察（同二进制、同 Sim 内存）

Stage-B 相对 Stage-A 的平均周期加速 **1.46×**，最好用例 `t26_rmw2` 为 **2.49×**。

| 用例 | Stage-A 周期 | Stage-B 周期 | 加速 |
| --- | --- | --- | --- |
| t10_stack | 65 | 31 | 2.10× |
| t11_callret | 55 | 40 | 1.38× |
| t12_sse_mem | 70 | 36 | 1.94× |
| t13_sse_arith_mem | 76 | 39 | 1.95× |
| t14_pushf | 54 | 29 | 1.86× |
| t15_paging | 116 | 88 | 1.32× |
| t16_fetch_map | 66 | 59 | 1.12× |
| t17_muldiv | 23 | 22 | 1.05× |
| t18_div | 29 | 28 | 1.04× |
| t19_dual | 46 | 45 | 1.02× |
| t1_alu | 40 | 39 | 1.03× |
| t20_fanout | 29 | 22 | 1.32× |
| t21_indirect | 32 | 27 | 1.19× |
| t22_jmprem | 28 | 20 | 1.40× |
| t23_cmpmem | 42 | 32 | 1.31× |
| t24_alumem | 49 | 29 | 1.69× |
| t25_rmw | 50 | 27 | 1.85× |
| t26_rmw2 | 107 | 43 | 2.49× |
| t27_loopne | 41 | 32 | 1.28× |
| t28_xchg | 44 | 22 | 2.00× |
| t29_xadd | 31 | 18 | 1.72× |
| t2_loop | 118 | 103 | 1.15× |
| t3_branch | 45 | 44 | 1.02× |
| t4_movx | 40 | 39 | 1.03× |
| t5_flags | 49 | 48 | 1.02× |
| t6_stress | 203 | 174 | 1.17× |
| t7_mem | 70 | 34 | 2.06× |
| t8_fwd | 49 | 26 | 1.88× |
| t9_sse | 52 | 51 | 1.02× |

说明：Stage-A 为严格顺序、单发射；Stage-B 为乱序执行 + 双提交。由于译码/派发仍为 1 宽，
双发射机会有限（fan-out 唤醒、前端气泡后可同拍发射两条），加速主要来自乱序唤醒、
store/load 转发与提前执行。

## 4. 已知限制

1. **前端宽度**：1 宽译码/派发，双发射无法常态化；未实现 6 宽前端与 8 宽退休。
2. **精确异常（Stage-B）**：无 IDT/LIDT/IRET；缺页与非法指令在乱序核中直接 fatal。
3. **指令覆盖**：字符串指令（MOVS/STOS/LODS/SCAS/CMPS）、内存间接 CALL 尚未实现。
4. **存储层次**：模拟器为 16MB 平坦内存 + 128-bit dmem 端口，无 L1/L2、预取、非对齐跨行拆分。
5. **系统**：单核、无 SMP、无 VMX/SVM，MSR 仅覆盖测试所需子集。
6. **参数规模**：ROB 32 / IQ 8 / PRF 64-16-32，小于昆明湖 V3 的 160 / 8-wide / 224-192-128。

## 5. 与昆明湖 V3 的差距

| 维度 | 本实现 (v3.8) | 昆明湖 V3 目标 | 后续动作 |
| --- | --- | --- | --- |
| 前端宽度 | 1 宽译码 | 6 宽译码 | 预译码 + 指令队列 + 多路译码 |
| 退休宽度 | 2 条/拍 | 8 条/拍 | 扩 ROB 到 160、提交端口到 8 |
| ROB/IQ | 32 / 8 | 160 / 大容量 | 参数化扩容、分 bank 发射队列 |
| PRF | 64 / 16 / 32 | 224 / 192 / 128 | 扩容并做多端口/分 bank |
| 执行单元 | 单 ALU + 单 SSE | 多 ALU/AGU/FP | 增加 ALU2/AGU/LSU 端口 |
| 异常 | Stage-A 精确；Stage-B fatal | 精确异常 + 中断 | 提交级异常 + IDT + 中断注入 |
| 存储 | 平坦内存 | L1/L2/L3 + 预取 | 加入 cache 模型与一致性 |

## 6. 复现命令

```bash
# 环境：sbt（Chisel 3.5.6/Scala 2.13）、Verilator 5.04、clang++、Unicorn 2.1.4
make decode-test                 # 693/693
make regress                     # Stage-A：37/37
make sys-test                    # 系统：10/10
python3 tools/ooo_regress.py     # Stage-B：29/29
sbt test                         # Scala 单元：21/21
```

## 7. 结论

- Stage-A 已满足目标中的“顺序流水线版本（可变长前端、整数+SSE2、异常/分页）”。
- Stage-B 已具备重命名、ROB、发射队列、PRF、乱序执行、双提交、分页与较完整的整数/SSE2 覆盖，
  并与 Stage-A 在 29 个用例上逐位对拍一致。
- 距离“6 宽、ROB 160、8 宽退休”的昆明湖级超标量仍有明确差距，路线见第 5 节。

## 8. 香山式逐指令 DiffTest（`emu`）

在原有“结束态对拍”之外，新增了与香山 `emu --diff --ref-so=...` 同形的逐指令 DiffTest：

```
build/emu  (Verilator TB: core_tb)
   │  每条退休指令：pc + 16×GPR + RFLAGS + (可选) op
   ▼
sim/difftest.cpp  ── dlopen/dlsym ──▶  build/ref/libx86ref.so
                                         （NEMU 风格 difftest ABI）
```

### 8.1 参考模型 ABI

`libx86ref.so` 导出与 NEMU/XiangShan 相同的符号：

| 符号 | 作用 |
| --- | --- |
| `difftest_init(int)` | 初始化参考机 |
| `difftest_memcpy(addr, buf, n, direction)` | 参考机内存读写（direction: 0=TO_DUT, 1=TO_REF） |
| `difftest_regcpy(void *state, direction)` | 同步/读取 GPR、RIP、RFLAGS（含预留 XMM 字段） |
| `difftest_exec(uint64_t n)` | 参考机单步 n 条 |
| `difftest_skip_ref(void)` | 跳过下一条（MMIO/异常指令等） |
| `difftest_raise_intr(uint64_t)` | 中断/异常注入（预留；系统态接入时使用） |

当前参考模型内部使用 **Unicorn** 提供 x86 指令语义；ABI 不变，后续可替换为手写 XEMU。

### 8.2 使用

```bash
make emu                                   # 构建 build/emu + build/ref/libx86ref.so
python3 tools/difftest.py tests/asm/t1_basic.S   # 生成 build/tests/t1_basic.bin
./build/emu -i build/tests/t1_basic.bin          # 香山风格：-i 指定镜像
./build/emu build/tests/t1_basic.bin             # 位置参数也支持
./build/emu -i build/tests/t1_basic.bin --no-diff   # 纯 RTL 仿真
./build/emu -i build/tests/t1_basic.bin --max-cycles=100000
make emu-test                              # 全部用户态用例逐指令对拍
```

参数风格与香山 `emu` 对齐：`-i IMAGE`（或 `--image=IMAGE`）指定镜像，
`--diff` 开启逐指令对拍，`--ref-so=PATH` 指定参考模型，另支持
`--max-cycles=N`、`--no-diff`；位置参数镜像仍然兼容。
注意 `build/emu` 是包装脚本（默认带 `--diff --ref-so=...`），底层仍是 Verilator 生成的
`build/obj_core/core_tb`。

`build/emu` 是一个包装脚本，等价于：

```bash
build/obj_core/core_tb --diff --ref-so=build/ref/libx86ref.so <image.bin>
```

### 8.3 结果

| 项目 | 结果 |
| --- | --- |
| 用户态用例（tests/asm，paging 关闭） | **37/37** 通过逐指令对拍 |
| 随机 fuzz（种子 1–1000） | **1000/1000** 通过逐指令对拍（XEMU） |
| 每条指令比较 | GPR×16 + XMM×16 + RFLAGS + PC + load 内存字节 |
| 修复后回归 | Stage-A 37/37、系统 10/10、OoO 29/29、解码 693/693、单元 21/21 |
| 系统模式 co-sim | `make sys-diff-test` 10/10（DUT vs XEMU 最终状态） |
| 每条指令比较 | GPR×16 + RFLAGS（未定义位按指令掩码/同步） |
| 内存 | 参考机自身执行 store；DUT load 与参考内存按字节校验 |

### 8.4 该模式发现并修复的真实 RTL 缺陷

逐指令对拍比结束态对拍更早暴露问题，本轮共修复：

1. **部分写转发错误**：`movw`/`movb` 后转发返回未合并的原始结果，导致后续读寄存器丢失高位。
2. **SAHF 误写 GPR**：SAHF 只应改 flags，却通过 WB 写回了 RAX。
3. **0xFE 组未强制 8 位**：`incb %al` 按 32 位计算 flags（SF/PF 错误）。
4. **ROL/ROR 错误改写 ZF/SF/PF**：旋转只定义 CF（计数为 1 时还有 OF）。
5. **MUL/IMUL 错误定义 SF/ZF/AF/PF**：这些位在乘法后是未定义，不应由 DUT 改写。
6. **PUSH/POP 操作数宽度**：`push %rbx` 存 64 位但 `pop %rcx` 按 32 位 load，丢失高 32 位。
7. **MOVD/MOVQ 高 64 位**：Stage-A 标量合并保留旧高位；Intel 规则应清零（REX.W 为 64 位，否则 32 位）。
8. **移位计数为 0**：x86 规定 flags 不变，原实现会清 CF。
9. **QNaN indefinite 符号**：x86 无效运算（0/0、负数 sqrt 等）产生 `0xFFF8...`，原实现产生正 qNaN。

### 8.5 测试计划

完整分层、fuzz 生成策略、未定义行为处理与覆盖矩阵见
[emu_testplan.md](emu_testplan.md)。

### 8.6 限制与后续

- Unicorn 不模拟 guest 分页/IDT 异常，`--diff` 目前只覆盖 **paging 关闭的用户态用例**；系统测试仍由 `make sys-test` 覆盖。
- MMIO 停机写不进入参考模型；HLT 由 DUT 单独处理。
- 分支/CALL/RET 等“在 EX 重定向但不产生 commit”的指令由参考模型 catch-up 执行。
- 未逐指令比较 XMM；后续可接入 `diff_state.xmm`。
- 后续可用手写 XEMU 替换 Unicorn 参考模型，ABI 与 DUT 侧无需改动。
