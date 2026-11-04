# X86-KMH 微架构设计文档

| 项目 | 内容 |
| --- | --- |
| 文档版本 | v0.2（与当前源码同步） |
| 文档日期 | 2026-11-04 |
| 文档状态 | Stage-A 为功能基线，Stage-B v1 为乱序机制原型 |
| 参考文档 | [PRD.md](PRD.md)、[stage-a.md](stage-a.md)、[README.md](README.md) |
| 参考微架构 | 香山昆明湖 V3（结构对齐，规模按 x86 现实裁剪） |

> 本文只描述代码中真实存在的结构。未实现、未验证或仅规划的内容一律标注“未实现/下一阶段”，不把 PRD 目标参数写成现状。Stage-A 指 `X86Core` 顺序核，Stage-B 指 `OooCore` 乱序核 v1。

---

## 1. 总体结构与设计目标

### 1.1 目标与关系

设计目标按优先级为：**架构正确性**（与 Unicorn 结束状态对拍）、**结构完整性**（整数 + SSE2 + 分页 + 异常闭环）、**可演进性**（解码器输出与微架构无关的 `Uop`，前后端可替换）。首版不追求商业 x86 性能，不支持 x87/AVX/MMX、实模式/保护模式、多核、虚拟化与 Linux 启动。

Stage-A 与 Stage-B **共享前端、替换后端**：

```
                    ┌──────────────────────────┐
   16B 取指窗口 ───▶│ frontend/Decoder.scala   │──▶ Uop（与微架构无关）
                    └────────────┬─────────────┘
              ┌──────────────────┴──────────────────┐
    ┌─────────▼──────────┐               ┌──────────▼─────────┐
    │ Stage-A  X86Core   │               │ Stage-B  OooCore v1│
    │ IF/ID→EX→MEM→WB    │               │ Rename→ROB→IQ      │
    │ MMU/SSE2/IDT       │               │ 单 ALU/顺序提交     │
    └─────────┬──────────┘               └──────────┬─────────┘
              │                                     │
   core_tb + difftest.py                 ooo_tb + ooocmp.py
              └────────────── 架构状态对比 ─────────┘
```

- Stage-A 是唯一能跑完整测试集的功能基线；Stage-B v1 只接收 ALU/分支类 `Uop`，用于验证重命名、ROB、IQ、检查点恢复的**机制正确性**，访存/SSE/栈/乘除/系统指令全部显式拒绝。
- Stage-A 同时是 Stage-B 的黄金模型：`tools/ooocmp.py` 用同一 flat binary 跑 A/B 两核并比对最终架构状态。
- 两者共用 `Decoder`、`Uop`、`IntAlu`、Verilator 内存模型与 flat binary 约定。

### 1.2 模块层次与当前参数

```
src/main/scala/x86/
├── Config.scala（object X86Config）        复位向量/栈顶/停机 MMIO/取指窗口
├── frontend/Decoder.scala                  变长解码器（A/B 共用）
├── isa/{Uop,Constants,DecodeTables}.scala  Uop、操作码枚举、操作码 ROM
├── core/                                   Stage-A：X86Core、IntAlu、SseAlu、
│                                           FpUnit、Mmu、RegFile、Bundles
├── ooo/                                    Stage-B：OooCore、OooTop
└── elaborate/Emit*.scala                   Verilog 生成入口
```

| 参数 | Stage-A | Stage-B v1 |
| --- | --- | --- |
| 取指 / 译码 | 32B 请求、16B 窗口 / 1 条/周期 | 同 Stage-A |
| 提交 | 1 条/周期，按序 | 1 条/周期，按序 |
| GPR / XMM | 16 / 16 | 16 / 0（调试口固定 0） |
| 物理寄存器 | 无（直接架构寄存器） | GPR 64 项；flags 16 项 |
| ROB / IQ | 无 | 32 项 / 统一 8 项 |
| 执行单元 | 1×IntAlu + 1×SseAlu(EX) + 1×SseAlu(MEM) | 1×IntAlu，单发射端口 |
| 访存 / TLB | 1 未完成请求 / 32 项直接映射 4KB | 无 |
| 分支预测 | 静态不跳转，EX 冲刷 | JMP 跳转、Jcc 不跳转 |

---

## 2. Stage-A 顺序流水线

### 2.1 流水级与停顿

```
   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
PC▶│ IF / ID      │──▶│ EX           │──▶│ MEM          │──▶│ WB           │
   │ 32B 取指缓冲 │   │ IntAlu/SseAlu│   │ MMU + dmem   │   │ 合并写回     │
   │ 16B 变长解码 │   │ 分支/地址生成 │   │ load/store   │   │ trace/异常   │
   └──────────────┘   └──────────────┘   └──────────────┘   └──────────────┘
          ▲                  │                  │
          └──── 重定向 ◀─────┴──── 访存停顿 ◀───┘
```

```
frontStall = !canUse || memWait || exHasMem || sysActive || stopFetch
入 ID/EX 条件 = !frontStall && !exRedirect && !halted && (!idEx.valid || exLatch)
```

取指窗口未命中、访存未完成、系统 FSM 忙或已停机时前端整体停住；`idEx` 未被消费时不会覆盖。

### 2.2 取指

- 复位 PC=`0x400000`；`lineBase={pc[63:4],4'b0}` 按 16B 对齐发起 `imem` 请求，响应 256 位（32B）。
- 请求时锁存 `fetchAddr`，响应返回写入 `ibuf/ibufBase`，避免重定向与响应竞争。
- `canUse = ibufValid && pc∈[ibufBase, ibufBase+16)`：**只消费 16B 窗口**。每进入新的 16B 区域就重新发一次 32B 取指，有效带宽是“每 16B 一次请求”，不是持续 16B/周期。
- 32B 缓冲允许 15B 长指令跨 16B 边界，只要起点在当前窗口内。
- 无 I-Cache、无预取、无 SMC 检测；任何重定向都清 `ibufValid` 并从新 PC 重取。

### 2.3 变长解码

解码器输入 `pc` 与 16 字节窗口，组合产生 `Uop/len/illegal`：

| 步骤 | 实现 |
| --- | --- |
| 前缀扫描 | 最多 5 次迭代；识别 0x66/0x67/0xF0/0xF2/0xF3/段前缀与 REX(0x40–0x4F) |
| REX 规则 | REX 必须最后；之后再出现 legacy 前缀会清 `rexValid`；取最后一个 REX |
| 越界/长度 | 偏移 14 仍是前缀，或长度 >15 → `illegal` |
| 操作码 | 一字节 map 与 0F map；0F38/0F3A 直接 illegal；80/81/83/C0/C1/D0–D3/F6/F7/FE/FF 与 0F 组走 group ROM |
| 寻址/立即数 | ModRM/SIB/disp8/disp32、RIP 相对、寄存器直接、立即数形式 |
| 宽度 | 默认 32；0x66→16；REX.W→64；32 位写零扩展，8/16 位写保留高位 |
| 输出 | 一条 x86 指令对应一个 macro-op，不拆微操作、无 uop cache |

未实现：0x67 地址大小前缀会被记录 `mem.addr32`，但地址仍按 64 位计算，**32 位回绕未实现**；前缀数 >5 不支持；字符串/RMW 可被解码器识别，但核心按未实现指令拒绝。

### 2.4 寄存器堆与转发

`GprFile` 16×64 位、组合读 7 口（a、b、base、index、RAX、RDX、RSP）、2 写口；`XmmFile` 16×128 位、2 读 1 写。操作数在 **EX** 级读取，离开流水线的结果靠转发可见：

| 来源 | 条件 | 目标 |
| --- | --- | --- |
| EX/MEM | `valid && !isLoad && writesReg` 且索引匹配 | GPR 目的 |
| MEM/WB | `valid && writesReg` 且索引匹配 | GPR 目的（含 load） |
| EX/MEM、MEM/WB | 栈操作且索引 RSP | `rspNext`（PUSH/POP/CALL/RET） |
| EX/MEM、MEM/WB | `writeHi` 且索引 RAX/RDX | MUL/DIV/RDMSR 高半结果 |
| EX/MEM、MEM/WB | XMM 目的匹配 / `wf` 为真 | XMM / RFLAGS |

- **load 结果不从 EX/MEM 转发**，但可从 MEM/WB 转发；访存期间前端停顿，保证下一条指令进入 EX 时数据可用。
- 无 scoreboard/互锁表；正确性依赖“访存停顿 + 旁路优先级”，不是通用动态调度。
- 优先级固定为 EX/MEM > MEM/WB > 寄存器堆。

### 2.5 EX / MEM / WB

**EX**：`IntAlu` 纯组合完成算术、逻辑、移位/循环移位、乘除、LEA、SETcc、CMOVcc、Jcc 条件。MUL/IMUL 一操作数形式为 RAX×src→RDX:RAX，IMUL 支持 2/3 操作数；DIV/IDIV 为 128÷64，除零或商溢出置 `divError`（后续 #DE）。乘除法用组合除法器**单周期完成**，功能正确但时序代价大。有效地址为 `base+index*scale+disp` 或 `RIP+len+disp`；PUSH/CALL 用 `rsp-8`。分支在 EX 解析并立即重定向、冲刷 `idEx`；JMP/CALL 支持相对与寄存器间接，内存间接未实现。MOV CR 写、WRMSR、SYSCALL、SYSRET、INVLPG 通过 `taken/target` 串行化；RET 在 MEM 响应时重定向。

**MEM**：访存先经 MMU 翻译（停机 MMIO 除外），再发一次 `dmem` 请求，`memWait` 保持到响应。`DMemReq` 为 64 位地址、128 位写数据、16 位字节掩码、5 位 size；同一时刻只有一个未完成请求。load 按 `memsz` 做零/符号扩展；128 位 SSE load 按指令标量零扩展或整块写入。store 数据来自 ALU、栈值、PUSHF 或 XMM。**停机 MMIO**：`0xF0000000` 的写在翻译前拦截，低 64 位作为 `exitCode` 并停机；`HLT` 走正常提交路径。无 D-Cache、无 Store Buffer、无乱序访存，按程序序强序完成。

**WB**：按 `opsz` 合并写回（8/16 位保留高位，32 位零扩展）；写口 1 写普通 GPR 目的，写口 2 写 RDX（MUL/DIV/CDQ）或 RSP（栈操作）。SYSCALL 写 RCX/R11，CPUID 分两拍写 EAX/EBX/ECX/EDX，异常入口/IRETQ 写 RSP。`flagsReg` 在 WB 更新；HLT 置停机；解码器报 `illegal` 时**直接 fatal 停机，不是 #UD**。输出 `CommitTrace` 供 `TRACE` 打印，difftest 未消费。

### 2.6 SSE2 与浮点单元

| 类别 | 已实现 | 说明 |
| --- | --- | --- |
| 128 位传送 | MOVAPS/MOVUPS/MOVDQA/MOVDQU/MOVSS/MOVSD、MOVD/MOVQ | 标量形式保留目的高 64/96 位；load 标量零扩展 |
| 位逻辑 | AND/ANDN/OR/XOR 的 PS/PD、PAND/POR/PXOR | 128 位按位；内存源位逻辑未实现（§6） |
| 整数 SIMD | PADDD/PSUBD | 4×32 位 lane，模 2³² 回绕 |
| 比较 | COMISS/COMISD、UCOMISS/UCOMISD | 只改 CF/PF/ZF，OF/SF/AF 保持；unordered 置 CF=ZF=PF=1 |
| 浮点算术 | ADD/SUB/MUL/DIV/SQRT/MIN/MAX 的 PS/PD/SS/SD | f32/f64，soft-float |
| 标量转换 | CVTSI2SS/SD、CVTTSS2SI/CVTTSD2SI、CVTSS2SD/CVTSD2SS | 有符号整数源；截断越界返回 0x8000… |
| packed 转换 | CVTDQ2PS、CVTPS2DQ、CVTDQ2PD、CVTPD2DQ、CVTTPD2DQ | 见下方精度说明 |

`FpUnit` 提供 addSub（对阶/粘位/RNE）、mul、div、sqrt、min/max、intToFloat、floatToIntTrunc、f32↔f64。语义：**固定 RNE**，无 MXCSR 舍入模式；**次正规输入按零处理（FTZ）**，结果下溢输出零；NaN 统一规范 qNaN，不保留 payload；不产生 #XM、不更新 MXCSR 异常标志；±0/Inf/NaN 基本传播已处理。已知偏差：CVTPS2DQ/CVTPD2DQ 用**截断**而非 RNE（测试用整数值规避）；CVTSS2SI/CVTSD2SI 非截断形式未实现。内存源浮点算术在 MEM 级复用第二份 `SseAlu`，面积换正确性。未实现：SHUF/UNPCK/PSHUFD、向量移位、MOVMSK/PMOVMSKB、MASKMOVDQU、LDMXCSR/STMXCSR、DAZ/FTZ 控制位、浮点异常报告。

### 2.7 访存与 4 级分页 / TLB

```
VA[63:48] ──规范检查──▶ 非规范 → #GP
   │
   ▼
┌─────────────────────────────┐  hit   ┌──────────────┐
│ TLB 32 项，直接映射          │───────▶│ PA = PFN|off │
│ index = VA[16:12]           │        └──────────────┘
└──────────────┬──────────────┘
               │ miss
               ▼
   PML4(CR3[51:12]) → PDPT → PD → PT
   每级 1 次 ptw 读；PS=1 的大页 → #PF(RSVD)
```

- 仅 4KB 页；PML4/PDPT/PD 的 PS=1 一律 #PF（错误码 RSVD 置 1），2MB/1GB 大页未实现。
- TLB 32 项直接映射，完整 40 位 VPN 作 tag；命中检查 W 与 NX。CR3 写或 INVLPG 均**整表失效**（无按页失效）。
- 规范检查只在 `CR0.PG=1` 时执行；错误码按 `[I/D,RSVD,U/S,W/R,P]` 组装，U/S 恒 0。
- **取指与数据共享 MMU**：`mmuOwner` 仲裁（数据优先、取指等待），取指按 16B 对齐的 `lineBase` 翻译后再发 imem 请求；翻译失败以向量 14、错误码 bit4=1 投递。分页关闭时取指恒等映射，不占用 MMU。限制：只翻译一行，32B 窗口跨 4KB 页未做拼接；共享 MMU 下取指 walk 不可取消，期间数据访问有界等待。
- `CR0.WP` 已接入但未参与判定，当前所有写都要求页表 W=1（等价 WP=1）；无 PCID/ASID/global/page-walk cache；一次 miss 为 4 次页表读 + 1 次数据访问。

### 2.8 异常、IDT 与 IRETQ

| 异常 | 触发 | 处理 |
| --- | --- | --- |
| #DE (0) | DIV/IDIV 除零或商溢出 | `divError`→FSM |
| #BP (3) | INT3 | FSM |
| #UD (6) | UD2，或解码成功但核心标记 `unsupported` | FSM |
| #GP (13) | MMU 规范地址检查失败 | FSM |
| #PF (14) | 缺页/权限/大页 | FSM |

解码器 `illegal`（0F38/0F3A、超长）**直接 fatal**，不经过 IDT；只有“已解码但未实现”走 #UD。

入口 FSM（`sysOp=0`）8 步：①读门低 8B；②读高 8B 并拼 handler；③压 SS 占位 0；④压旧 RSP；⑤压 RFLAGS；⑥压硬编码 CS=8；⑦压异常 PC；⑧有错误码的向量压错误码；最后跳 handler、清 IF、`RSP-=40/48`。IRETQ（`sysOp=2`）依次读 RIP/CS/RFLAGS/RSP/SS，恢复 RIP/RFLAGS/RSP，CS/SS 读出后丢弃。限制：忽略 selector/DPL/type/IST/P/IDT limit；CS 固定 8、SS 固定 0；不换 CPL/栈、无 TSS/#DF；异常期间再次异常被忽略；**#PF 不更新 CR2**；无用户态检查。

### 2.9 系统指令

| 指令/寄存器 | 实现 |
| --- | --- |
| MOV CR0/2/3/4 | 读/写；写 CR3 触发 TLB 整表失效并串行化 |
| RDMSR/WRMSR | EFER(0xC0000080)、STAR(81)、LSTAR(82)、SFMASK(84)；其他读 0、写忽略 |
| LIDT | 读 10 字节设置 IDTR base/limit；不检查 limit |
| SYSCALL | `RIP=LSTAR`，`RCX=下一条 RIP`，`R11=RFLAGS`，`RFLAGS&=~SFMASK`；不换 CS/SS/RSP |
| SYSRET | `RIP=RCX`，`RFLAGS=R11`；不换 CPL |
| CPUID | 叶子 0/1/0x80000000/0x80000001；0x80000008 未实现（返回 0） |
| INVLPG / HLT | 整表失效并串行化；停止取指，提交到 WB 后停机 |
| CLI/STI/CLD/STD/CLC/STC/CMC、PUSHF/POPF | 改 RFLAGS 或压栈/出栈 |
| ENDBR64 / UD2 | NOP / #UD |

`STAR` 只写不读，`EFER.SCE` 不检查，SYSCALL/SYSRET 的段选择子与使能语义未实现。

### 2.10 顺序核的已知边界

| 类别 | 未实现/限制 |
| --- | --- |
| 内存目的/源 ALU | `add %rax,(%rbx)`、`cmp/test` 内存源等全部 `unsupported` |
| 字符串/控制流 | MOVS/STOS/LODS/SCAS/CMPS、REP、LOOP、INT n、内存间接 CALL/JMP |
| 标志/杂项 | XADD、LOOPNE/LOOPE/JRCXZ；RCL/RCR 按 ROL/ROR 近似 |
| 寄存器 | AH/BH/CH/DH 未映射（无 REX 的 4–7 号字节寄存器被当作 SPL/BPL/SIL/DIL） |
| 系统 | LGDT、用户态、2MB 大页、CR2 自动更新、跨页取指 |
| 性能 | 无 I/D Cache；每 16B 一次取指、访存约 2 拍停顿，IPC 约 0.3–0.6 |

---

## 3. Stage-B 乱序后端 v1

### 3.1 结构、参数与前端

```
PC▶┌──────────────┐  ┌──────────────┐  ┌──────────────────┐
   │16B 取指缓冲  │─▶│Decoder（1）  │─▶│Rename/Dispatch   │
   └──────────────┘  └──────────────┘  │RAT/FreeList/Busy │
                                       └────────┬─────────┘
                         ┌──────────────────────┴──────────────┐
                         ▼                                     ▼
                 ┌──────────────┐                     ┌──────────────┐
                 │ROB 32，顺序  │◀── done/value ──────│IQ 8 统一     │
                 │提交          │                     │最老就绪选择  │
                 └──────┬───────┘                     └──────┬───────┘
                        ▼ 1 条/周期                          ▼ 1 条/周期
                 ┌──────────────┐                     ┌──────────────┐
                 │Commit/释放旧  │                     │IntAlu×1      │
                 │物理寄存器     │                     │PRF(GPR/flags)│
                 └──────┬───────┘                     └──────────────┘
                        │ 提交时写内存
                        ▼
                 ┌──────────────┐   ┌──────────────┐
                 │MQ 8（按序）  │──▶│StoreBuffer 8 │
                 │load/store    │   │提交后写回     │
                 └──────────────┘   └──────────────┘
```

| 参数 | 值 | 参数 | 值 |
| --- | --- | --- | --- |
| ROB | 32 项 | 统一 IQ | 8 项 |
| 整数 PRF | 64 项 | XMM PRF | 32 项（16 逻辑） |
| flags PRF | 16 项（1 个逻辑 flags） | 访存 MQ / SB | 8 / 8 |
| 取指 | 16B 窗口/32B 请求 | 译码/发射/提交 | 各 1 条/周期 |
| 访存队列 MQ | 8 项、按序 | Store Buffer | 8 项、提交时写内存 |

前端复用 Stage-A 的取指缓冲与解码器，`canUse` 仍限制在 16B 窗口。没有 BPU/FTQ，预测规则写死：`predTaken=(op==JMP)`；`predTarget=立即数?pc+len+imm:pc+len`；Jcc 一律预测不跳转，不维护历史、不做目标预测。

### 3.2 重命名（PRF/FreeList/RAT/Busy）

- `ratG[16]`：GPR→6 位物理号；`ratF[16]`：只用 `ratF(0)` 作为 flags 的单一逻辑寄存器；`ratX[16]`：XMM→5 位物理号。
- `freeG[64]` 初始 0–15 占用、16–63 空闲；`freeX[32]` 初始 0–15 占用；`freeF[16]` 初始 0 占用、1–15 空闲。分配用 `PriorityEncoder`，无空闲栈；提交时释放 `oldDst/oldDstX/oldDstF`。
- `busyG/busyF/busyX` 标记等待写回的物理寄存器；发射要求所有源 busy=0。
- `needG=writesGprOp(uop)`（MOV/算术/移位/SETcc/CMOVcc/XCHG/LEA 等）；`needF=writesFlagsOp(uop)`（写标志的 ALU）；`JCC/CMOVCC/SETCC/ADC/SBB/RCL/RCR` 读 flags。
- flags 是“单逻辑寄存器 + 16 个物理版本”，**不做 CF/ZF/SF/OF 分域重命名**，所有写 flags 与后续读 flags 形成串行依赖；无 Move 消除、指令融合、RAB。

### 3.3 ROB、发射、执行与提交

**ROB**：每项含 `valid/seq/Uop`、`hasDst/pdst/oldDst`、`hasDstF/pdstF/oldDstF`、`done/value/flagsVal/taken/target`、`isBranch/predTaken`，以及 `cpRatG[16]/cpRatF[16]/cpFreeG[64]/cpFreeF[16]/cpBusyG[64]/cpBusyF[16]` 检查点。`tailSeq/headSeq` 为 32 位序号，ROB 索引取低 5 位；ROB 满（`tail-head>=32`）或 IQ 满时停止分派。

**发射**：统一 IQ 不按 FU 分组；就绪条件为所有源物理寄存器 `busy=0`（LEA 额外需要 base/index）。选择策略为**最老就绪优先**：`iqSel(i)=ready(i) && 对所有就绪 j，seq(i)<=seq(j)`，用组合比较实现，没有年龄矩阵。每周期最多发射 1 条，单 ALU 端口；源操作数在发射级组合读 PRF；无 wakeup/select 分离、无投机唤醒、无旁路网络。

**执行/写回**：`IntAlu` + `SseAlu` 各 1 个端口。整数支持 MOV/MOVZX/MOVSX/MOVSXD/LEA/算术/逻辑/移位/循环移位/SETcc/CMOVcc/XCHG/Jcc/JMP/HLT/NOP/ENDBR；SSE 支持 128 位寄存器传送、位逻辑、PADDD/PSUBD、FP 四则/开方/min/max、标量与 packed 转换、COMIS*/UCOMIS*（写 flags）。SSE 目的可为 XMM、GPR（MOVD/MOVQ/CVT*2SI）或 flags（COMIS）；**SSE 内存操作数暂不支持**。LEA 只用重命名后的 base/index，**不处理 RIP 相对（结果会错，v1 未测试）**；8/16 位写按 `rsA` 旧值合并高位。写回在**发射同周期**完成：写 `prfG/pdst`、清 busy、置 `rob.done`；flags 同理。分支在发射级解析方向和目标，不等到提交。

**提交**：只有 ROB head 且 `done` 才能提交，每周期 1 条；释放 `oldDst/oldDstF`、清项、递增 head；HLT 在提交时置 `halted`。没有独立的“架构 RAT”：`ratG/ratF` 始终是投机映射，误预测靠检查点回滚。`exitCode` 固定 0；无停机 MMIO、无 commit trace。

**访存（v1.1）**：MOV/MOVZX/MOVSX/MOVSXD 的内存形式进入独立 MQ，按**程序顺序**执行（head 一条/周期），因此 load/store 之间无需年龄矩阵或违例恢复：
- store 在 MQ 执行级算地址/数据，写入 StoreBuffer 并置 ROB `done`，**提交级**才真正写 dmem（保证精确异常/顺序提交语义）；提交需等 store 响应。
- load 执行时先查 StoreBuffer，同地址同宽度直接转发；否则读 dmem，按 opsz 做零扩展/符号扩展/旧值合并。
- MQ 用 head/tail 指针（容量 7，留一槽判满），不再维护计数，避免计数漂移；误预测时按 seq 清除年轻项并重算 tail。
- 限制：不支持交叉宽度转发（如 8B store 后 4B load）、未实现 ALU 内存源/目的、无 L1D/多端口、无 MSHR；MQ 按序执行限制访存并行度。
- **栈操作（v1.3/v1.4）**：PUSH/POP/CALL/RET 在 MQ 中特殊处理——派发时为隐式 RSP 分配新物理寄存器，MQ 执行级完成 RSP±8 与栈访存；POP 同时写目的寄存器与 RSP（ROB 增加 `hasDst2/pdst2/oldDst2`，提交释放两个旧映射，误预测一并归还）。StoreBuffer 转发按**最年轻匹配项**选择，避免同一地址多次 push 时取到旧值。
- **SSE 内存源运算（v1.6）**：ADD/SUB/MUL/DIV/SQRT/MIN/MAX 的内存形式进入 MQ，执行级实例化第二个 `SseAlu`，以 `prfX(oldDstX)` 为 a、加载数据为 b 计算结果并写回 XMM；标量结果仍按旧目的合并。
- **RIP 相对（v1.6）**：`uop.mem.ripRel` 在 LEA 与 MQ 地址生成时用 `pc+len` 作为基址，修复此前 RIP 相对地址被当作基址 0 的问题。
- **SSE 访存（v1.5）**：dmem/MQ/SB/ROB 统一加宽到 128 位，支持 MOVD/MOVQ/MOVDQ/MOVPS 的内存形式；load 按 opsz 零扩展（MOVSS/MOVSD 的高位清零），store 在提交级按 16/8/4 字节掩码写回。SSE 访存长度在**执行级由 uop 重新推导**（`sseSizeOf`），避免 MQ 字段在并发派发/执行同拍失配。
- **COMIS 内存源（v2.8）**：MQ 完成级用 `sseMem` 对寄存器源与内存值比较，flags 写 `prfF`。关键点是 `sseMem.flagsIn` 必须使用**派发时锁存的 flags 物理寄存器**（`psrcF`），而不是 0——否则 CF/PF/ZF/SF/OF 之外的 IF/bit1 会被清零，最终 RFLAGS 读回 0。Stage-A 侧同样在 MEM 级用 `sseMem.io.flags` 覆盖 `memWb.flags`。
- **间接 JMP/CALL（v2.5）**：`jmp *%reg` 以 `a.kind=Gpr` 进入 IQ，预测不跳转，发射级取 `rsA` 为目标并走检查点冲刷。`call *%reg` 进入 MQ：压入返回地址、更新 RSP，然后以目标寄存器触发重定向；重定向**不再与 MQ 弹出同拍**，而是由冲刷统一清空 MQ 并重置指针/计数。此前 CALLreg 同时命中通用 load 路径，导致一次多余读与并发弹出，是 head/tail 计数被破坏的根因。
- **双发射与双提交（v2.3）**：IQ 增加第二发射端口（独立 IntAlu/SseAlu），选择“第二老就绪”指令（排除分支/MUL/CR 等串行化操作）；两路同时写 PRF/ROB。提交级每拍最多退休 ROB head 与 head+1，stores 需 `storeDone` 后才退休。由于译码/派发仍是 1 宽，IQ 通常只有 1 条待发射指令，双发射主要发生在 fan-out 唤醒（一个写回同时点亮多个消费者）或前端气泡之后；`t20_fanout` 用于覆盖该路径。
- **DIV/IDIV 与 CDQ/CBW（v2.2）**：单操作数 DIV/IDIV 复用 MUL 的双目的三源路径（RAX/RDX/操作数），商写 RAX、余数写 RDX；CDQ/CQO 只写 RDX 高半，CBW/CWDE/CDQE 写 RAX。IntAlu 修复两处：CQO 的 `hiOut` 应为符号掩码（原为 ~mask，64 位时恒 0）；有符号除法的被除数/除数/商溢出检查用动态左移做符号扩展会因 Chisel 动态移位扩宽而丢失符号，改为按位宽静态符号扩展。修复后 Stage-A 的 `t36_div`/`t37_cqo` 与 Unicorn 一致，OoO `t18_div` 通过。
- **MUL/IMUL（v2.1）**：单操作数形式在派发时分配两个 GPR 物理寄存器（RAX、RDX），源为 RAX/RDX/显式操作数三路；执行用 `IntAlu` 的 oneOp 模式一次算出低/高半，写回两个 PRF；ROB 用 `hasDst2/pdst2/oldDst2` 记录 RDX 目的，提交释放两个旧映射。修复了两个重命名问题：通用 `needG` 重命名会把源寄存器误映射为 RAX 目的；ROB 的 `oldDst` 应取 RAX 旧映射而非源寄存器映射。DIV/IDIV 的 oneOp 路径仍有误报 #DE，暂按 #UD 快速失败。
- **取指分页（v2.0）**：前端新增独立 I-MMU 与 `iptw` 端口，取指前对 16B 行基址做地址翻译（状态机 idle→translate→imem req→resp），PTW 请求寄存一拍；缺页/非规范地址直接 fatal（OoO 尚无 IDT）。数据与取指各用一个 Mmu 实例（独立 TLB），CR3 写入时同时刷新。
- **MOV CR 与 MMU（v1.9）**：MOV CR 读回 CR 值；写 CR 只在 **ROB head** 才允许发射（x86 要求串行化，避免年轻指令先改 CR 影响更老的未完成访存），更新 CR 后触发一次串行化冲刷（复用检查点/失效逻辑，仅 RET 时重写 RSP 映射）。数据侧 `Mmu` 实例与 PTW 端口：MQ 访问前按 `meVaddr` 请求翻译，`mePaddr` 用于 dmem/SB；页表 walk 数据优先从 StoreBuffer（未提交页表写）转发；PTW 请求打一拍寄存，避免 walk 地址在采样时尚未稳定。
- **PUSHF/POPF（v1.7）**：PUSHF 从 flags 物理寄存器取值压栈；POPF 从栈加载后按 `0xED5` 掩码合并 flags（保留 TF/NT/保留位），并同时更新 RSP。POPF 在派发时为 flags 分配新的物理版本，提交释放旧版本。
- **CALL/RET（v1.4）**：直接 CALL 在 MQ 压入返回地址，按“预测跳转”处理（目标在译码已知，不冲刷）；RET 在 MQ 读返回地址，完成时从 MQ 触发重定向：恢复 RAT 后**重新写回 RET 的 RSP 映射**，失效所有年轻 ROB/IQ/MQ/SB 项，重取目标。MQ 改为计数式占用（`mqCount` 单表达式更新），修复此前 head/tail 指针在并发 push/pop/冲刷下失配的问题。

### 3.4 分支检查点与误预测冲刷

检查点粒度是**每条指令**：分派时把 `ratG/ratF/freeG/freeF/busyG/busyF` 全量复制进 ROB 项，等价于“每项都是潜在分支检查点”；但恢复时只使用 RAT 快照，free/busy 字段不整体回滚。误预测判定：

```
issueTaken      = (op==JMP) || (op==JCC && alu.branchTaken)
issueMispredict = issueValid && isBranch && (issueTaken =/= predTaken)
```

恢复动作在发射级同周期完成：①用 `cpRatG/cpRatF` 恢复 RAT；②对 ROB 中每个 `seq>分支 seq` 的有效项，把它的 `pdst/pdstF` 归还 FreeList 后置无效（**不恢复旧 busy 位**）；③IQ 中 `seq>分支 seq` 全部置无效；④`tailSeq:=分支 seq+1`（保留分支自身等待提交）；⑤`pc:=实际目标`、清 `ibufValid`、重新取指；⑥同周期阻止新分派。

只回滚 RAT、不整体回滚 FreeList/Busy 的原因：分支分派后更老的指令可能已经写回，恢复旧 busy 会把已完成的物理寄存器重新标成等待，导致死锁。因此采用“撤销被冲刷项的新分配”：把它们的 `pdst/pdstF` 归还 FreeList，busy 位保持当前值；被冲刷指令写入 PRF 的值也不回滚，但 RAT 恢复后不再被引用。`cpFreeG/cpFreeF/cpBusyG/cpBusyF` 仍会写入 ROB 但恢复路径未使用，后续可删除。**缺陷**：间接 JMP 的目标未参与比较（`predTarget` 固定 `pc+len`，而 `issueMispredict` 只比 `taken`），v1 测试未覆盖间接 JMP。每项全量快照仍使 ROB 存储面积远大于常规设计，下一阶段应改为仅分支处拍快照（RAB/检查点池）。

### 3.5 能力边界与与 Stage-A 的差异

状态：v3.8 两端均支持 XADD（内存 ← 内存+寄存器、寄存器 ← 原内存值，复用 RMW 通路并写 flags）。Stage-A 37/37、OoO 29/29 差分测试通过。

| 能力 | 状态 |
| --- | --- |
| 整数 ALU/分支 | 支持（含 MOVZX/MOVSX/MOVSXD/LEA/移位/CMOVcc/SETcc；XCHG 仅回写一个目的） |
| 乘除法 | 未实现：MUL/IMUL/DIV/IDIV 被拒绝 |
| 访存/栈/标志栈 | MOV/MOVZX/MOVSX/MOVSXD、SSE MOV 类与 SSE 算术/COMIS 内存源、RIP 相对、PUSH/POP/PUSHF/POPF、直接/寄存器间接/内存间接 CALL/RET/JMP、MOV CR0/CR3、数据+取指分页、MUL/IMUL/DIV/IDIV、CDQ/CBW、ALU 内存源与目的（含一元/移位）已支持；XCHG 内存形式仍缺；OoO 无 #PF 精确异常 |
| SSE/XMM | 寄存器形式已支持（XMM PRF/重命名、SseAlu、COMIS 写 flags）；SSE 内存操作数被拒绝 |
| 系统/异常/中断 | 未实现：CR/MSR/SYSCALL/CPUID/IRET/INT/UD2/字符串被拒绝；无 IDT |
| 分支预测 / 提交 trace / 停机 MMIO | 无 / 无 / 无 |
| 性能/分支恢复 | 单发射/单提交，小测试仅比 Stage-A 快 1–2 拍；分支冲刷仍在收敛 |

解码成功但 `!isAluOp(op)` 时，或解码器 `illegal`，会立即 `halted:=true; fatal:=true; fatalCode=#UD` 并记录 `fatalPc`（等待/超时问题已修复，K-16）。这与 Stage-A 的“unsupported 走 #UD 异常”不同：Stage-B 没有 IDT，直接停机。

| 维度 | Stage-A | Stage-B v1 |
| --- | --- | --- |
| 执行模型 | 顺序、单发射 | 乱序发射、顺序提交 |
| 寄存器 | 架构寄存器堆直接读写 | RAT + PRF + FreeList + Busy |
| 结果可见性 | EX/MEM、MEM/WB 旁路 | 发射时写 PRF，消费者下一周期读 |
| 分支 | EX 解析后冲刷 IF/ID | 发射级解析，恢复 RAT 并释放被冲刷项 pdst |
| 异常 / 访存 | IDT/IRETQ / MMU+dmem | 无异常，fatal 停机 / MQ+SB，无 MMU |
| SIMD | SSE2（含浮点） | SSE2 寄存器运算（无内存源） |

---

## 4. 与香山昆明湖 V3 的对照

下表把“公开的 V3 参数”“PRD 的 V1.0 目标”“当前 Stage-B v1 实现”分列，避免把目标当现状。V3 参数取自 PRD §2.2 的公开资料摘录。

| 维度 | 昆明湖 V3 | PRD V1.0 目标 | Stage-B v1 现状 |
| --- | --- | --- | --- |
| ISA | RV64GC + RVV | x86-64 + SSE2 | x86-64 整数子集 |
| 译码 / 重命名 / 分派 | 6 / 6 / 6 | 4 / 4 / 6 | 1 / 1 / 1 |
| ROB | 160 项，压缩，单项 ≤6 uop | 128 项，压缩，单项 ≤4 uop | 32 项，单项 1 uop |
| 退休宽度 | 8 条/周期 | 4 条/周期 | 1 条/周期 |
| 整数 PRF | 224 | 160 | 64 |
| 浮点/向量 PRF | 192 FP + 128 Vector | 128 XMM | 无（仅 16 个 flags 物理寄存器） |
| 发射队列 | 分布式，多组 | 2×ALU16、MUL12、2×LSU16、BR12 | 统一 8 项，单 ALU 端口 |
| 执行单元 | 多 ALU/FP/Vector | 3 ALU+1 MUL/DIV+2 AGU+1 ST+1 BR+1 SSE | 1 ALU |
| Load/Store、LQ/SQ、SBuffer | 3+2、LSQ 分队列、16 项 | 2+1、48/32、16×64B | 无 |
| L1I/L1D、L1 TLB、L2 TLB | 公开资料未列全、48 项全相联、多级 PTW | 32KB 8 路 / 32KB 8 路、ITLB32/DTLB48、1024 项 | 无 |
| 分支预测 | uBTB+aBTB+uTAGE+mBTB+TAGE+SC+ITTAGE+RAS | 同构简化版 | 无（JMP taken、Jcc not-taken） |
| 预测块 / RAB / Move 消除 | 32B、16 条 / 256 项 / 支持 | 16B、6 条 / 规划中 / P1 | 无 / 无 / 无 |

**差距分析**：

1. **前端宽度是首要差距**。V3 为 6 宽译码，当前 1 宽；x86 变长指令还需先定边界。v1 无预解码/指令队列，且每 16B 重取一次，前端吞吐远低于后端需求。M4 必须先做“取指→预解码→指令队列→4 宽译码”。
2. **ROB/PRF 规模差一个数量级**。32 项 ROB、64 项整数 PRF 只能覆盖很小窗口；V3 的 160 项压缩 ROB 与 224 项整数 PRF 支撑 8 宽退休。当前每项还带全量检查点，面积效率和恢复带宽都不适合直接放大。
3. **发射/执行/提交**。V3 是分布式 IQ + 年龄矩阵 + 多执行单元、8 宽退休；当前是 8 项统一 IQ、单 ALU、单发射、1 宽提交，且无 wakeup/select 分离与旁路网络，IPC 上限被单端口卡死。
4. **LSU/TLB 是最大功能缺口**。V3 有 3 load+2 store、64KB 4 路 D-Cache、48 项全相联 L1 TLB、多级 PTW、16 项 SBuffer；当前 Stage-B 完全没有访存，Stage-A 只有 32 项直接映射 TLB（取指/数据共享 MMU）、无 cache/SBuffer，取指只翻译单行。M5 的 LSU 工作量应单独估算。
5. **分支预测与验证规模**。V3 是多级 TAGE 家族 + RAS；当前无 BPU。验证目前是几十条汇编用例 + 693 条解码向量，V3 级验证需要随机指令流、长跑 difftest、覆盖率与性能计数器，尚未建立。

---

## 5. 验证方法

### 5.1 分层与用例

| 层 | 内容 | 入口 | 当前规模 |
| --- | --- | --- | --- |
| L1 解码 | 长度/合法性对拍 llvm-mc | `tools/gen_decode_tests.py`、`sim/decoder_tb.cpp` | 693 向量 |
| L1 单元 | 浮点/MMU 定向测试 | 见 stage-a.md | 18 项（浮点 14 + MMU 4） |
| L2 指令级 | Stage-A vs Unicorn 结束状态对拍 | `tools/difftest.py`、`sim/core_tb.cpp` | 37 个（含 t31–t45） |
| L3 系统 | 分页/异常/SYSCALL/CPUID/INVLPG/取指缺页/INT n | `tools/sysrun.py` + `*.expect.json` | 10 个 |
| L3 乱序 | Stage-B vs Stage-A 结束状态对拍 | `tools/ooocmp.py`、`sim/ooo_tb.cpp` | 21 个 |

**解码向量（693）**：llvm-mc 汇编生成 flat image 并记录 PC/长度/名称，`decoder_tb` 从 16B 窗口驱动 RTL，只比较**长度与合法性**，不比较 opcode/ModRM 语义。覆盖前缀、REX、ModRM/SIB/disp、RIP 相对、SSE 编码。最近一次 `693/693 passed`。

**Stage-A difftest**：`clang -target x86_64-unknown-none-elf` + `llvm-objcopy` 生成 flat binary；`core_tb` 装入 16MB 内存的 `0x400000`，执行到 `halted/fatal`；Unicorn 2.1.4 以相同镜像、`RIP=0x400000`、`EFLAGS=0x202` 启动，写 `0xF0000000` 时停止。比较 16 GPR、16 XMM、RIP、FLAGS（掩码 `0xFD5`）、内存哈希；失败时打印首个差异并保留 `.mem`。内存哈希为 FNV-1a，覆盖栈 `0x7F0000–0x800000` 与数据 `0x600000–0x610000` 各 64KB；RIP 允许“RTL 多走最后一条停机指令”的 1–15 字节窗口。**注意**：这是结束状态对拍，不是逐指令 trace 比对；`CommitTrace` 已存在但工具未使用，与 PRD REQ-VER-003 仍有差距。基线为 `tests/asm/t0_smoke`…`t21_sse_mem` 共 22 个；新增 `t31_negflag`（NEG/AF）与 `t32_std`（STD/CLD），当前基线+新增共 24 个。

**系统测试（10）**：t22_cr（CR）、t23_paging（4 级页表）、t25_trap（UD2/#UD/IRETQ）、t26_pf_trap（#PF/错误码）、t27_iret（IRETQ 恢复）、t28_syscall（LSTAR/SFMASK）、t29_cpuid（叶子/厂商/特性）、t30_invlpg（失效后重映射）、t31_fetchpf（取指 #PF，错误码 0x10，校验栈帧 RIP）、t32_intn（INT n 经 IDT 投递，校验保存 RIP 与 handler 直达）。每个用例有 `.expect.json`，`sysrun.py` 比对 `exit/halted/fatal/fatalCode/rip/gpr/xmm`；最近一次 9/9 通过。

**Stage-B 差分**：`ooocmp.py` 用同一 binary 分别跑 A/B 两核，比较 GPR、RIP、FLAGS 掩码、XMM 与**内存哈希**，并检查 B 核 `halted=1 && fatal=0`。`ooo_tb` 已实现 16MB 内存、dmem 读写、FNV-1a 内存哈希，并从 `prfX(ratX)` 导出 16 个 XMM。用例包括 `t1_alu`、`t2_loop`、`t3_branch`、`t4_movx`、`t5_flags`、`t6_stress`、`t7_mem`、`t8_fwd`、`t9_sse`、`t10_stack`（PUSH/POP）、`t11_callret`（直接 CALL/RET）、`t12_sse_mem`（SSE 128/64/32 位访存）、`t13_sse_arith_mem`（SSE 算术内存源 + RIP 相对 LEA）、`t14_pushf`（PUSHF/POPF 与 RFLAGS 恢复）、`t15_paging`（数据页重映射）、`t16_fetch_map`（取指重映射）、`t17_muldiv`、`t18_div`、`t19_dual`、`t20_fanout`、`t21_indirect`、`t22_jmprem`、`t23_cmpmem`、`t24_alumem`、`t25_rmw`、`t26_rmw2`、`t27_loopne`、`t28_xchg`、`t29_xadd`；最近一次 29/29 通过。

### 5.2 如何运行

```bash
make compile        # 编译 Chisel
make rtl            # 生成 build/rtl/X86Top.v
make core-sim       # 构建 Stage-A Verilator 模型
make decode-test                             # 693 条解码向量
make regress                                 # tests/asm 全量 difftest
make sys-test                                # 9 个系统测试（依赖 core-sim）
make ooo-test                                # 构建 A/B 两核并跑 tests/ooo

make test TEST=tests/asm/t1_basic.S
python3 tools/difftest.py tests/asm/t13_sse.S
python3 tools/sysrun.py tests/system/t23_paging.S
python3 tools/ooocmp.py tests/ooo/t1_alu.S
python3 tools/regress.py t1_basic t2_loop    # 只跑指定用例
```

### 5.3 最近一次本地结果快照（2026-11-04）

| 套件 | 结果 | 备注 |
| --- | --- | --- |
| 解码 | 693/693 | 长度/合法性 |
| Stage-A 全量（t0–t21 + t31–t37 + t39） | 30/30 | 结束状态 + 内存哈希 |
| 系统测试 | 10/10 | expect.json（含取指 #PF、INT n） |
| Stage-B 整数+访存+SSE+栈+调用+分页+乘除+双发射+间接跳转+COMIS/ALU 内存/RMW/循环/XCHG/XADD 用例 | 29/29 | 与 Stage-A 结束状态 + 内存哈希 + XMM 对拍 |

> 结果以实际运行 `regress.py`、`sysrun.py`、`ooo_regress.py` 为准。

---

## 6. 已知问题与后续路线

### 6.1 已知问题

| 编号 | 问题 | 影响 / 处理 |
| --- | --- | --- |
| K-01 | 非法编码直接 fatal，而非 #UD | 异常路径覆盖不全；解码级 illegal 送入异常 FSM |
| K-02 | #PF 不写 CR2；IDT 不检查 DPL/IST/type/limit | 与 SDM 有偏差；M5 前补 CR2 与门检查 |
| K-03 | 取指只翻译 16B line，32B 窗口跨页未拼接；取指/数据共享单 MMU 且 walk 不可取消 | 跨页取指会取错；数据访问有界等待；M5 接 ITLB/独立 PTW |
| K-04 | 无用户态/CPL、TSS、#DF | 只能 ring 0；列为 V1.x |
| K-05 | 大页、LGDT、2MB/1GB、PCID 未实现 | 页表覆盖受限；大页优先 |
| K-06 | 字符串、内存间接 CALL 未实现 | 指令覆盖不完整；按 P0 列表补 |
| K-07 | AH/BH/CH/DH 未映射；RCL/RCR 近似；XCHG 只回写一个目的 | 字节寄存器/进位循环/交换语义错误 |
| K-08 | SSE 内存源位逻辑退化为整块 load；SHUF/UNPCK/向量移位缺失 | 结果错误或 fatal；补 `SseAlu` 路径 |
| K-09 | MXCSR 未建模：固定 RNE、FTZ、规范 NaN、无异常标志 | 浮点边界与真机有差异；先补舍入/DAZ/FTZ |
| K-10 | CVTPS2DQ/CVTPD2DQ 用截断而非 RNE | 与 Unicorn 默认舍入不一致 |
| K-11 | SYSCALL/SYSRET 不换 CS/SS/RSP，STAR/SCE 未用 | 仅 ring 0 快速调用；与用户态一起做 |
| K-12 | difftest 只比结束状态，无逐指令 trace | 难以定位中间偏差；消费 `CommitTrace` |
| K-13 | Stage-B 无系统/异常；无 #PF 精确异常（缺页 fatal）；一元/移位内存目的未实现 | 验证子集；M4/M5 补齐 |
| K-14 | Stage-B 全量保存检查点字段但仅回滚 RAT；单发射/单提交；无 BPU | 面积大、性能低；改 RAB/分支快照并扩宽 |
| K-15 | 无综合/时序/覆盖率数据 | 不能判断可综合性；M6 补 Yosys 与覆盖率 |
| K-16 | Stage-B 的非 ALU 指令（XADD/STD/CLC/CLD/LAHF/栈/字符串等） | 已改为分派前显式 fatal（#UD，快速失败），不再死锁；后续逐步实现 |

### 6.2 里程碑状态

| 里程碑 | 状态 | 说明 |
| --- | --- | --- |
| M0 工具链 / M1 解码 | 已完成 | sbt/Chisel 3.5.6、Verilator、Unicorn；693/693 |
| M2 顺序整数核 | 已完成 | 整数 difftest 基线通过 |
| M3 SSE2 + 系统 | 基本完成 | SSE2、分页（数据+取指 #PF）、IDT/IRETQ、MSR/SYSCALL、CPUID、INVLPG 已实现；LGDT/用户态待补 |
| M4 乱序前端 | 🚧 v1 | 重命名/FreeList/ROB/统一 IQ 已实现（1 宽）；无 FTQ/BPU/4 宽译码 |
| M5 乱序后端 | 🚧 v1.5 | 单 ALU+SseAlu、顺序提交、分支/RET 冲刷、MQ+SB（含 SSE 128 位）访存已实现；无多端口 LSU/分布式 IQ/多执行单元 |
| M6 定版 | 未开始 | 无综合/性能/覆盖率报告 |

### 6.3 M4：乱序前端计划

1. **FTQ + 预解码**：记录起始 PC、预测块字节范围、方向/目标、RAS 动作；预解码标注边界、长度、是否分支。
2. **分支预测**：先 uBTB + RAS + 静态方向，再两级 BTB/TAGE；提交级训练，误预测重定向。
3. **取指/译码解耦**：32B/周期取指、指令队列、4 宽译码，处理跨 16B/cacheline 指令。
4. **重命名/分派 4/6**：RAT 扩到 16 GPR + 16 XMM + flags；PRF 160/128；FreeList 改栈式。
5. **ROB 128 项**：压缩项（≤4 uop）、4 宽提交；检查点改为分支时拍快照（RAB/检查点池），去掉每项全量快照。
6. **恢复路径**：RAT/FreeList/Busy/ROB/IQ 一致回滚；异常与中断精确恢复。

退出标准：重命名/ROB/检查点单元测试通过；Stage-A 全部用例在 Stage-B 顺序模式下仍全绿；前端不停留在“每 16B 重取”。

### 6.4 M5：乱序后端计划

1. **分布式 IQ**：2×ALU(16)、MUL/DIV(12)、2×LSU(16)、BR(12)；年龄矩阵 + wakeup/select 分离。
2. **执行单元**：3 ALU + 1 MUL/DIV + 2 AGU + 1 ST + 1 BR + 1 SSE；写回仲裁与旁路网络。
3. **LSU**：LQ 48 / SQ 32、SBuffer 16、2 load + 1 store；非对齐拆分/合并；store→load 转发；MMU 接入 ITLB/DTLB 与 PTW。
4. **异常/中断**：提交级精确异常、CR2、IDT 门检查、#DF 兜底；中断在指令边界采样。
5. **提交**：压缩 ROB、4 宽退休、commit trace 供逐指令 difftest。
6. **验证**：10⁷ 条指令随机/定向 difftest；全部 Stage-A 用例；IPC ≥ 1.5× Stage-A。

### 6.5 优先级建议

| 优先级 | 事项 |
| --- | --- |
| P0（M3 收尾） | K-01 非法编码、K-02 CR2/IDT 门检查、K-03 跨页取指 |
| P0（M4） | FTQ/预解码、4 宽译码、RAB 检查点、ROB 128、PRF 160 |
| P1（M5） | LSU/LQ/SQ/SBuffer、分布式 IQ、SSE 执行单元、精确异常 |
| P1（验证） | 逐指令 trace difftest、随机指令流、长跑 |
| P2 | 大页、uop cache、用户态、性能计数器、综合/覆盖率 |

---

## 7. 附录

### 7.1 文件索引

| 文件 | 作用 |
| --- | --- |
| [`docs/PRD.md`](PRD.md) / [`docs/stage-a.md`](stage-a.md) | 需求与 Stage-A 实现说明 |
| [`src/main/scala/x86/Config.scala`](../src/main/scala/x86/Config.scala) | `X86Config`：复位向量、栈顶、停机 MMIO、取指窗口 |
| [`src/main/scala/x86/frontend/Decoder.scala`](../src/main/scala/x86/frontend/Decoder.scala) | 变长解码器 |
| [`src/main/scala/x86/isa/Uop.scala`](../src/main/scala/x86/isa/Uop.scala) 等 | Uop 定义、操作码枚举、操作码 ROM |
| [`src/main/scala/x86/core/X86Core.scala`](../src/main/scala/x86/core/X86Core.scala) | Stage-A 顺序流水线、异常 FSM |
| [`src/main/scala/x86/core/IntAlu.scala`](../src/main/scala/x86/core/IntAlu.scala) | 整数 ALU、标志、乘除、分支条件 |
| [`src/main/scala/x86/core/SseAlu.scala`](../src/main/scala/x86/core/SseAlu.scala) / [`FpUnit.scala`](../src/main/scala/x86/core/FpUnit.scala) | SSE/SSE2 执行单元与 soft-float |
| [`src/main/scala/x86/core/Mmu.scala`](../src/main/scala/x86/core/Mmu.scala) | 4 级页表 walker + TLB |
| [`src/main/scala/x86/core/RegFile.scala`](../src/main/scala/x86/core/RegFile.scala) / [`Bundles.scala`](../src/main/scala/x86/core/Bundles.scala) | 寄存器堆、存储器/PTW/trace 接口 |
| [`src/main/scala/x86/ooo/OooCore.scala`](../src/main/scala/x86/ooo/OooCore.scala) / [`OooTop.scala`](../src/main/scala/x86/ooo/OooTop.scala) | Stage-B 乱序核与顶层 |
| [`sim/core_tb.cpp`](../sim/core_tb.cpp) / [`sim/ooo_tb.cpp`](../sim/ooo_tb.cpp) / [`sim/decoder_tb.cpp`](../sim/decoder_tb.cpp) | 三个 Verilator harness |
| [`tools/difftest.py`](../tools/difftest.py) / [`sysrun.py`](../tools/sysrun.py) / [`ooocmp.py`](../tools/ooocmp.py) | A/Unicorn、系统测试、A/B 对拍 |
| [`tools/regress.py`](../tools/regress.py) / [`ooo_regress.py`](../tools/ooo_regress.py) / [`gen_decode_tests.py`](../tools/gen_decode_tests.py) | 回归入口与解码向量生成 |
| [`Makefile`](../Makefile) | 构建/仿真/回归命令 |
| [`tests/asm/`](../tests/asm) / [`tests/system/`](../tests/system) / [`tests/ooo/`](../tests/ooo) | 汇编、系统、乱序用例 |

### 7.2 术语与修订

| 术语 | 含义 |
| --- | --- |
| Macro-op / Uop | 一条 x86 指令对应一个 `Uop`，未拆微操作 |
| RAT / PRF / FreeList | 逻辑→物理映射表 / 物理寄存器堆 / 空闲物理寄存器集合 |
| ROB / IQ / RAB | 重排序缓冲 / 发射队列 / 重命名缓冲（下一阶段） |
| TLB / PTW | 地址翻译缓存 / 页表 walker |
| difftest / FTZ / RNE | 差分测试 / flush-to-zero / 就近偶数舍入 |

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v0.1 | 2026-11-04 | 首版：Stage-A、Stage-B v1、V3 对照、验证与路线 |
| v0.2 | 2026-11-04 | 按当前源码校正取指/转发/异常/SSE 细节；补充实测问题 |
| v0.3 | 2026-11-04 | 同步 NEG/AF 与 STD 常量修复；Stage-A 更新为 24/24；Stage-B 分支冲刷标注为收敛中 |

---

*本文与源码同步维护；修改微架构时请同时更新对应章节与测试记录。*
