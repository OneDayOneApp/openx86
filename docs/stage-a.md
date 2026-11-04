# Stage-A 顺序核：实现说明与覆盖范围

本文记录当前已实现的 Stage-A（顺序流水线）核心的结构、指令覆盖、验证结果与已知限制。

## 1. 流水线结构

```
        ┌──────────────┐   ┌──────────┐   ┌────────┐   ┌───────────┐
 PC ───▶│ Fetch/Decode │──▶│   EX     │──▶│  MEM   │──▶│ WB/Commit │
        │ 32B 取指缓冲区 │   │ ALU/分支  │   │ Load/  │   │ 寄存器写回  │
        │ 16B 变长解码   │   │ 地址生成  │   │ Store  │   │ 提交 trace │
        └──────────────┘   └──────────┘   └────────┘   └───────────┘
```

| 阶段 | 实现 |
| --- | --- |
| Fetch | 按 16B 行对齐取 32B（`io.imem.resp.data` 256 位）；`fetchAddr` 在请求时锁存，避免重定向与响应竞争 |
| Decode | 组合译码：前缀扫描（≤5）、REX、0F map、ModRM/SIB/disp、立即数；输出 `Uop` |
| EX | 32 个 GPR 读口（a/b/base/index/rax/rdx/rsp）+ 转发；ALU、分支条件、有效地址、栈地址 |
| MEM | 单请求 16B 数据接口；load/store 暂停前端一拍；`0xf0000000` 写作为停机 MMIO；RET 在响应时重定向 |
| WB | 按操作数宽度合并写回；第二写口用于 RDX（MUL/DIV/CDQ）与 RSP（PUSH/POP/CALL/RET） |

关键控制：

- **顺序提交**：所有架构状态在 WB 生效；不支持推测。
- **前递**：EX/MEM、MEM/WB 覆盖 GPR、RSP（栈操作）、RAX/RDX（乘除多结果）。
- **部分宽度写**：8/16 位写保留高位，32 位写零扩展，64 位全写。
- **停机**：`HLT` 或写 `0xf0000000`（值写入 `exitCode`）。

## 2. 已支持指令（Stage-A）

| 类别 | 指令 |
| --- | --- |
| 传送 | MOV（r/m、imm、8/16/32/64）、MOVZX/MOVSX/MOVSXD（寄存器与内存源）、LEA、XCHG（寄存器） |
| 算术 | ADD/ADC/SUB/SBB/AND/OR/XOR/CMP/TEST、INC/DEC/NEG/NOT |
| 乘除 | MUL/IMUL（1/2/3 操作数）、DIV/IDIV、CBW/CWDE/CDQE、CWD/CDQ/CQO |
| 移位 | SHL/SHR/SAR/ROL/ROR/RCL/RCR（imm8/1/CL；RCL/RCR 当前按 ROL/ROR 处理） |
| 控制流 | JMP/Jcc（rel8/rel32、寄存器间接）、CALL（rel32、寄存器间接）、RET/RET imm16、SETcc、CMOVcc |
| 栈 | PUSH/POP（寄存器、立即数）、PUSHF/POPF、CALL/RET 的栈操作 |
| 标志 | CLC/STC/CMC/CLD/STD/CLI/STI、LAHF/SAHF、PUSHF/POPF |
| 访存 | MOV 的 1/2/4/8 字节 load/store；ModRM/SIB/disp8/disp32/RIP 相对；非对齐访问 |
| 其他 | NOP、ENDBR64、HLT、UD2（#UD）、INT3（#UD 停机） |
| 系统（已实现） | MOV CR0/CR2/CR3/CR4、4 级分页（4KB）+ 32 项 TLB、数据与取指地址翻译（共享 MMU 仲裁）、取指 #PF、LIDT、IDT 异常投递、IRETQ、RDMSR/WRMSR（EFER/STAR/LSTAR/SFMASK）、SYSCALL/SYSRET、CPUID（0/1/0x80000000/0x80000001）、INVLPG |
| 系统（待实现） | LGDT、用户态与权限检查、2MB 大页、跨页取指拼接 |
| SSE2（已实现） | MOVD/MOVQ（GPR↔XMM）、MOVAPS/MOVUPS/MOVDQA/MOVDQU/MOVSS/MOVSD、ANDPS/PD、ANDNPS/PD、ORPS/PD、XORPS/PD、PAND/POR/PXOR、PADDD/PSUBD、COMISS/COMISD/UCOMISS/UCOMISD、XMM 寄存器堆与 128 位访存 |
| SSE2 浮点（已实现） | ADD/SUB/MUL/DIV/SQRT/MIN/MAX（PS/PD/SS/SD，soft-float，RNE）、CVTSI2SS/SD、CVTTSS2SI/CVTSD2SI、CVTSS2SD、CVTSD2SS、CVTDQ2PS/CVTPS2DQ/CVTDQ2PD/CVTPD2DQ/CVTTPD2DQ |
| SSE2 访存 | 寄存器与内存源算术、128 位 load/store、标量零扩展/高位保留 |
| SSE2（待实现） | SHUF/UNPCK、向量移位、MXCSR 舍入模式/DAZ/FTZ、内存源 CVT |

## 3. 验证

| 项目 | 结果 |
| --- | --- |
| 解码器长度/合法性对拍 | 693 / 693 通过（llvm-mc 编码语料） |
| 汇编 difftest | 37 / 37 通过（新增 NEG/AF、STD/CLD、LAHF/SAHF、LOOP/LOOPNE/LOOPE/JRCXZ、DIV/IDIV、CQO、内存间接 JMP、COMIS/ALU 内存源、ALU 内存目的/一元/移位、XCHG/XADD） |
| 浮点/MMU 单测 | 18 / 18 通过（浮点 14 + MMU 4） |
| 系统测试 | 10 / 10 通过（CR、分页、#UD/#PF、IRETQ、SYSCALL、CPUID、INVLPG、取指 #PF、INT n） |
| 参考模型 | Unicorn 2.1.4（QEMU TCG），x86-64 |
| 仿真器 | Verilator 5.04，周期级 C++ harness |
| 测试命令 | `make decode-test`、`make regress` |

测试集：

| 用例 | 覆盖 |
| --- | --- |
| t0_smoke | 最小启动、立即数、算术、停机 MMIO |
| t1_basic | 综合整数序列、栈、调用、乘除 |
| t2_loop | 循环与 ZF 分支 |
| t3_store | 存储/加载往返 |
| t4_control | 16 种条件、嵌套/间接调用与跳转 |
| t5_bits | 8/16/32 位部分写、移位、扩展、setcc/cmov |
| t6_mem | 非对齐、SIB、RIP 相对、多宽度访存 |
| t7_movsx | 符号扩展 |
| t8_flags | 条件码与标志位组合 |
| t9_branches | 分支走向计数 |
| t10–t12 | 分支/前递/部分写回归 |
| t13_sse | XMM 传送、整数 SIMD、位运算、COMISD、128 位内存访存 |
| t14–t18 | 标量/向量浮点四则、min/max、符号与 64 位转换、f32↔f64、packed 双精度 |
| t19–t21 | sqrt（PS/PD/SS/SD）、packed 整数↔浮点转换、内存源 SSE 算术 |
| t22–t30 | MOV CR、分页、LIDT、异常投递/返回、MSR/SYSCALL、CPUID 特性位、INVLPG 失效后重映射 |
| t31–t45 | NEG 的 AF/CF、STD/CLD、LAHF/SAHF、LOOP/LOOPNE/JRCXZ、DIV/IDIV、CQO、内存间接 JMP、COMIS/ALU 内存源、ALU 内存目的（含一元/移位 RMW）、XCHG/XADD |

## 4. 已知限制（M3 处理）

1. **SSE2 覆盖面**：四则、开方、min/max、标量/packed 转换、内存源算术已实现；重排/解包（SHUF/UNPCK）与向量移位尚未实现（按 #UD 停机）。
   浮点单元固定 round-to-nearest-even，忽略 MXCSR 舍入模式/DAZ/FTZ；次正规输入按零处理（FTZ），NaN 负载统一为规范 qNaN；CVTPS2DQ/CVTPD2DQ 目前按截断（Unicorn 默认 RNE，测试使用整数值规避）。
2. **分页与异常往返已实现**：数据地址经 4 级页表翻译；异常可经 IDT 投递并 IRETQ 返回；取指缺页已支持（16B 行翻译，跨页窗口未拼接）；尚缺大页、用户态/CPL 切换、LGDT。
3. **RMW 指令未实现**：`add %rax, (%rbx)` 等内存目的 ALU 操作按 #UD 停机；`CMP/TEST` 的内存源形式同样暂不支持。
4. **字符串指令未实现**：MOVS/STOS/LODS/SCAS/CMPS 及 REP 按 #UD。
5. **间接 CALL/JMP 内存操作数未实现**（寄存器形式已支持）。
6. **字符串指令** 未实现（LOOPNE/LOOPE/JRCXZ、XCHG、XADD 已实现）；ALU 内存源/目的（含一元、移位、ROL/ROR/RCL/RCR）已实现（MEM 级第二 IntAlu，目的形式走“load→ALU→store”两阶段）；LOOP/LAHF/SAHF/INT n、DIV/IDIV、CQO、内存间接 JMP 已实现。修复：CQO 的 RDX 应为全 1（原 ~mask=0）；有符号除法符号扩展的动态移位缺陷；JMP/CALL 内存形式曾被误判为 store 并在 unsupported 列表中被拒绝。COMIS 内存源比较已在 MEM 级实现（`sseMem` 计算 flags，WB 写回），`t38_cmpmem` 通过。XCHG 内存形式的三个缺陷已修复：转发目的索引误用 `a.idx`、WB 写使能要求 `a.kind=Gpr`、访问长度取到 0。
7. **AH/CH/DH/BH**：解码器当前把无 REX 的 4–7 号字节寄存器映射到 SPL/BPL/SIL/DIL；高 8 位寄存器待补。
8. **RCL/RCR**：暂以 ROL/ROR 近似（不带 CF 环移）。
9. **SSE 内存源算术**：`addps (%rax), %xmm0` 等尚未实现（寄存器形式已支持）。
10. **性能**：每 16B 一次取指、访存停顿约 2 拍，IPC 约 0.3–0.6；Stage B 替换后端。

## 5. 文件索引

| 文件 | 说明 |
| --- | --- |
| `src/main/scala/x86/frontend/Decoder.scala` | 变长解码器 |
| `src/main/scala/x86/isa/DecodeTables.scala` | 操作码 ROM 与分组表 |
| `src/main/scala/x86/isa/Uop.scala` | 微操作定义 |
| `src/main/scala/x86/core/IntAlu.scala` | 整数 ALU 与标志 |
| `src/main/scala/x86/core/X86Core.scala` | 顺序核 |
| `src/main/scala/x86/core/RegFile.scala` | GPR/XMM 寄存器堆 |
| `sim/core_tb.cpp` | Verilator harness（含内存模型与 trace） |
| `tools/gen_decode_tests.py` | 解码语料生成 |
| `tools/difftest.py` | Unicorn difftest |
| `tools/regress.py` | 回归入口 |
