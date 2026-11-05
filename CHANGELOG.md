## Unreleased — L1：真实内核进入 startup_64

### 新增

- `--kernel FILE`：按 Linux x86 启动协议加载 bzImage 或 ELF vmlinux
- ELF64 加载（`PT_LOAD` → `p_paddr`），跳过内核内解压器
- 引导器预先建立 4 级恒等页表与 `CR0.PG`
- x87 最小实现（`D8–DF` 分组解码、栈机、控制/状态字、四则、比较、`FNINIT`）
- 段覆盖前缀（`FS`/`GS`）参与有效地址；`WRMSR` 写 FS/GS base
- `MOV sreg`（`8C`/`8E`）、`RETF`（`CA`/`CB`）、`LEAVE`/`ENTER`

### 修复

- `AL/eAX, imm` 立即数 ALU 的 16 条指令（`04/05/0C/0D/…/3C/3D`）完全缺失
- `LGDT`/`LIDT`/`RETF` 使用物理访问导致开页后越界，改为虚拟访问
- `HLT` 不再视为程序结束（内核 idle 循环会执行 `HLT`）
- `0F 22` 写 `CRn` 与 `0F 20` 读 `CRn` 的标志位区分

### 性能

- UART 输入轮询从"每条指令一次 `read()`"改为每 1024 条一次
  （4000 万条指令：14s → 4.4s，约 9 MIPS）

## Unreleased — L0 设备模型

### 新增

- 端口 IO：`IN`/`OUT`/`INS`/`OUTS`（8/16/32 位，支持 `REP`）与分发框架
- 16550 UART：完整寄存器组，THR 直通 stdout，stdin 轮询进 RBR 并触发 IRQ4
- 8259A PIC：ICW1-4、OCW1/OCW2/OCW3、IRR/ISR、主从级联
- 8254 PIT：模式 2/3、计数器锁存读、通道 0 驱动 IRQ0
- 未映射端口读返回全 1，写为无副作用
- 物理内存可配置（`-DXEMU_MEM_SIZE_MB=N`，默认 16MiB）
- 新测试：`t50_uart`、`t51_pit`、`t52_portio`；`make xemu-dev-test` 5/5

### 说明

- PCA/PIC/PIT 由 `xemu_after_inst()` 在每条指令后推进，
  IRQ 在指令边界经 PIC 仲裁后注入，避免撕裂

## Unreleased — Bochs 独立黄金基准

### 新增

- 构建 Bochs 2.8（`--enable-debugger`，x86-64，nogui）作为独立仲裁基准
  - `make bochs-build` / `make bochs-img` / `make bochs-test`
- `tools/bochs/boot.S`：512 字节长模式引导桩（4 级页表、PAE、OSFXSR、LME+PG）
- `tools/bochs/run.py`：调试器驱动（loadmem + RFLAGS stub + 单步 + 状态抓取）
- `tools/bochs/difftest.py`：XEMU vs Bochs 对拍，39/39 通过

### 修复（Bochs 接入过程中发现）

- 2MiB 大页物理基址必须 2MiB 对齐（`0x100000` → `0x200000`），否则触发 RSVD 位 #PF
- 执行 SSE 前必须置位 `CR4.OSFXSR`/`OSXMMEXCPT`
- 早期 halt 的镜像需按实际步数对齐，避免把"提前停机"误判为状态偏差

## Unreleased — 文档整理

### 变更

- XEMU 文档统一收纳到 `xemu/docs/`：
  - `plan.md`（原 `docs/xemu-plan.md`）
  - `roadmap.md`（原 `docs/xemu-roadmap.md`）
  - `linux-plan.md`（原 `docs/xemu-linux-plan.md`）
  - `linux-testplan.md`（新增：Linux 启动测试计划，156 个用例）
  - `README.md`（XEMU 文档索引）
- `docs/README.md` 与根 `README.md` 的 XEMU 条目改为指向 `xemu/docs/`

# Changelog

## Unreleased — P3 系统模式（CPL3/TSS/外部中断/timer）

### 新增

- `LGDT` / `LIDT` / `LTR`，GDT 描述符解析
- CPL0/CPL3：特权级切换经 TSS `RSP0` 换栈；同层陷阱不换栈
- IDT 门类型/DPL 解析：中断门清 IF，陷阱门保留 IF；`INT n` 做 DPL 检查
- 外部中断注入 `xemu_request_irq()` 与每指令 `xemu_timer_tick()`
- Timer MMIO（`0xE0000000`）：周期/使能/计数/清 pending，到期置 IRQ 32
- `SYSCALL`/`SYSRET` 按 STAR 切换 CS/SS/CPL
- `make xemu-sys-test`：t40_cpl3、t41_timer **2/2**
- IRET 恢复 CPL；trap frame 与 RTL 微码逐字节对齐

## Unreleased — Unicorn ISA 对拍 + NEMU 风格 xemu CLI

### 新增

- `tools/isa_diff.py` + `make isa-diff`：Unicorn 作为逐指令标准
  - 71 个指令模板 × 随机 GPR/XMM/flags/内存
  - 标量 SS/SD NaN 语义、未定义 flags 掩码、MOVD/MOVQ 高位容差
  - 当前 **14300/14300** 通过
- 修复：三操作数 IMUL 立即数为 0、CVT 越界返回 indefinite、标量 FP NaN 比较
- P2：字符串 MOVS/STOS/LODS/SCAS/CMPS + REP/REPNE（DF、RCX 计数）
- 修复 CVTSS2SD/CVTSD2SS NaN payload 转换（quiet 位与载荷移位）
- P2：BSF/BSR、BT/BTS/BTR/BTC、CMPXCHG/CMPXCHG8B/16B
- 修复 CMPXCHG flags 方向与 32 位目标零扩展
- P2：SSE2 整数（PADDB/W/Q、PSUBB/W/Q、PCMPEQB/W/D、PCMPGTB/W/D、PSHUFD/HW/LW、
  PUNPCKL/H、PMOVMSKB、MOVNTDQ）与内存间接 CALL/JMP

### 新增

- `build/xemu`：独立 NEMU 风格运行器（不依赖 DUT/Verilator）
  - `-i IMAGE`、`-b`、`--max-steps=N`、`--trace`
  - 交互式 monitor：`si`、`info r`、`x`、`q`
  - 失败时打印最后 16 条指令 ring buffer
- `make xemu-bin`

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
