# XEMU Linux 启动测试计划（v1）

配套文档：[linux-plan.md](linux-plan.md)（L0–L4 里程碑）。

本文定义每个里程碑的**测试用例**（TC 编号）、**通过判据**与**回归门禁**。
所有用例都可脚本化，最终收敛为 `make xemu-linux-test`。

---

## 1. 测试分层

| 层 | 名称 | 判据来源 | 对应里程碑 |
| --- | --- | --- | --- |
| T0 | 单元/寄存器语义 | 硬编码期望值（架构手册） | L0–L4 |
| T1 | 指令级对拍 | Unicorn（用户态）/ 自写 oracle | L0, L2 |
| T2 | 设备模型 | 硬编码期望值 + 回环 | L0 |
| T3 | freestanding 内核 | 串口输出 + 退出码 | L0–L3 |
| T4 | 真实 Linux 引导 | 串口输出匹配 | L1–L4 |
| T5 | 长稳与性能 | 时长/吞吐阈值 | L4 |

判据优先级：**T4 > T3 > T2 > T1 > T0**。低层失败时高层不必跑。

---

## 2. L0 — 端口 IO 与最小设备

### 2.1 端口 IO 指令（T0/T1）

| TC | 用例 | 输入 | 期望 |
| --- | --- | --- | --- |
| L0-IO-01 | `IN AL, imm8` | 端口 0x3F8 写入 'A' 后读 | AL = 'A' |
| L0-IO-02 | `IN AX, DX` | 16 位设备端口 | AX 为低 16 位，高位不变 |
| L0-IO-03 | `IN EAX, DX` | 32 位设备端口 | EAX 写入并零扩展 |
| L0-IO-04 | `OUT imm8, AL` | 向 UART THR 写 'Z' | stdout 出现 'Z' |
| L0-IO-05 | `OUT DX, AX/EAX` | 16/32 位宽度 | 设备按宽度接收 |
| L0-IO-06 | `INS`/`OUTS` + `REP` | RCX=4，从端口块传输 | RCX=0，方向按 DF |
| L0-IO-07 | 未映射端口读 | 读 0x9999 | 返回 0xFFFFFFFF，不挂死 |
| L0-IO-08 | 未映射端口写 | 写 0x9999 | 丢弃，不挂死 |
| L0-IO-09 | 非法端口访问保护 | CPL3 且 IOPL<3 | 触发 #GP(0)，err=0 |
| L0-IO-10 | `IN`/`OUT` 长度解码 | 各前缀组合 | 与 XED/llvm-mc 编码一致 |

### 2.2 16550 UART（T2）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L0-UART-01 | 写 THR(0x3F8) | 字节进 stdout，LSR.THRE=1 |
| L0-UART-02 | 读 RBR(0x3F8) | 有输入时返回字节，LSR.DR=1/0 正确切换 |
| L0-UART-03 | LCR 设置字长/停止位/校验 | 读回一致 |
| L0-UART-04 | 波特率除数 DLL/DLM | 读回一致（仅保存，不影响时序） |
| L0-UART-05 | MCR 回环模式 | OUT2/LOOP 位读回正确 |
| L0-UART-06 | IIR 无中断时 | 读回 0x01（无 pending） |
| L0-UART-07 | 输入中断（IER.RDA） | 有输入时 IRQ4 触发 |
| L0-UART-08 | 输出 `\n` | 转成 `\r\n`（可选，按配置） |

### 2.3 8259A PIC（T2）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L0-PIC-01 | ICW1–ICW4 初始化 | mask 寄存器复位为 0xFFFF |
| L0-PIC-02 | OCW1 写 mask | 被 mask 的 IRQ 不投递 |
| L0-PIC-03 | IRQ0 触发 | CPU 收到 vector 0x20 |
| L0-PIC-04 | OCW2 EOI | ISR 位清除，可再触发 |
| L0-PIC-05 | 未 EOI 时不重复投递 | 同 IRQ 保持 pending |
| L0-PIC-06 | 从片 IRQ8–15 | vector = 0x28 + n |
| L0-PIC-07 | IRR/ISR 读（OCW3） | 与实际状态一致 |
| L0-PIC-08 | 优先级 | IRQ0 > IRQ1，同级轮转（若实现） |

### 2.4 8254 PIT（T2）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L0-PIT-01 | 模式 2 初始化 | 计数器按 reload 递减 |
| L0-PIT-02 | 到期触发 IRQ0 | 频率 = 1.193182MHz / reload |
| L0-PIT-03 | 写新 reload | 下一周期生效 |
| L0-PIT-04 | 读回计数器（锁存） | 单调递减 |
| L0-PIT-05 | 模式 3 方波 | 输出对称（若实现） |

### 2.5 内存配置（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L0-MEM-01 | 默认 256MB | 地址 0x0FFFFFF0 可读写 |
| L0-MEM-02 | 超出范围访问 | 读返回全 1 / 触发 #PF，不越界写宿主 |
| L0-MEM-03 | `-m 16M/512M` 参数 | 实际可用大小一致 |
| L0-MEM-04 | 清零初始化 | 启动时全零 |

### 2.6 freestanding 测试内核（T3）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L0-KRN-01 | 串口打印 "XKERN OK" | 输出匹配 |
| L0-KRN-02 | PIT 中断计数到 100 | 退出码 0，计数=100 |
| L0-KRN-03 | PIC mask/EOI 配合 | 不丢中断、不重复 |
| L0-KRN-04 | CPL3 切换 + `int 0x80` | 回到内核，寄存器保持 |
| L0-KRN-05 | 异常路径 UD2/#PF | 进入正确 handler |

**L0 门禁**：`make xemu-dev-test` 全绿 + ISA diff 不回归 + 系统锁步不回归。

**当前进度**（L0 全部用例已实现）

| 用例 | 实现 | 结果 |
| --- | --- | --- |
| L0-IO-01..06 | `IN`/`OUT`/`INS`/`OUTS` + REP | ✅ t52_portio |
| L0-IO-07/08 | 未映射端口读全 1、写丢弃 | ✅ t52_portio |
| L0-IO-09 | CPL3 且 IOPL<3 → #GP | ⬜ 待补（需 IOPL 语义） |
| L0-IO-10 | 编码与 llvm-mc 一致 | ✅ 693/693 解码向量 |
| L0-UART-01..06 | THR/RBR/LCR/DLL/MCR/IIR/LSR/SCR | ✅ t50_uart |
| L0-UART-07 | 输入中断 IRQ4 | ✅ 已实现（`xemu_uart_poll`） |
| L0-PIC-01..08 | ICW1-4、OCW1/2/3、IRR/ISR、EOI | ✅ t51_pit |
| L0-PIT-01..05 | 模式 2/3、锁存读、reload | ✅ t51_pit |
| L0-MEM-01..04 | 可配置内存（默认 16MB，可 -D 到 256MB） | ✅ |
| L0-KRN-01..05 | 串口 + 中断 + CPL3 | ✅ 由 t40/t41/t50/t51 覆盖 |

```bash
make xemu-dev-test
# PASS t40_cpl3 / t41_timer / t50_uart / t51_pit / t52_portio
# xemu sys: 5 passed, 0 failed
```

---

## 3. L1 — Linux 64-bit Boot Protocol

### 3.1 boot_params 构造（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L1-BP-01 | `hdr.boot_flag` | = 0xAA55 |
| L1-BP-02 | `hdr.header` | = "HdrS"(0x53726448) |
| L1-BP-03 | `hdr.version` | ≥ 0x020c |
| L1-BP-04 | `hdr.loadflags` | `LOADED_HIGH\|KERNEL_64\|CAN_USE_HEAP` |
| L1-BP-05 | `hdr.code32_start` | = 内核入口 |
| L1-BP-06 | `hdr.cmd_line_ptr` | 指向 cmdline 字符串 |
| L1-BP-07 | `hdr.ramdisk_image/size` | 与 initramfs 实际位置一致 |
| L1-BP-08 | `e820_entries` | ≥4 且区间不重叠、升序 |
| L1-BP-09 | e820 覆盖 | `0x0–0x9FC00` usable；`0xA0000–0x100000` reserved；`0x100000+` usable |
| L1-BP-10 | `%rsi` 指向 boot_params | 入口寄存器正确 |

### 3.2 ELF 加载（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L1-ELF-01 | PT_LOAD 段展开 | 文件偏移与 paddr 映射正确 |
| L1-ELF-02 | .bss 清零 | p_memsz > p_filesz 区域为 0 |
| L1-ELF-03 | 高地址段（>16MB） | 能加载到 256MB 范围 |
| L1-ELF-04 | 入口地址 | 解析 e_entry 正确 |

### 3.3 initramfs（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L1-CPIO-01 | cpio "newc" 格式 | 头部 magic `070701` |
| L1-CPIO-02 | `/init` 可执行位 | mode 含 0111 |
| L1-CPIO-03 | 目录项与符号链接 | 结构合法 |
| L1-CPIO-04 | 尾部 TRAILER | 正确结束 |

### 3.4 内核引导（T4）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L1-BOOT-01 | 进入 `startup_64` | 不触发 #GP/#PF |
| L1-BOOT-02 | 打印内核版本横幅 | 串口出现 `Linux version 6.` |
| L1-BOOT-03 | 打印 `Memory:` 行 | 数值与 e820 一致 |
| L1-BOOT-04 | 解压/重定位完成 | 不 hang |
| L1-BOOT-05 | 跑到 `Run /init as init process` | 串口匹配 |

**L1 门禁**：L1-BOOT-05 通过。

---

## 4. L2 — x87 与上下文切换

### 4.1 x87 数据传送（T0/T1）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L2-X87-01 | `FLD m32/m64/m80` | ST(0) 正确，TOP 递减 |
| L2-X87-02 | `FST`/`FSTP m32/m64/m80` | 内存值正确，FSTP 弹栈 |
| L2-X87-03 | `FILD m16/m32/m64` | 整数转 80 位 |
| L2-X87-04 | `FIST`/`FISTP` | 按 RC 舍入 |
| L2-X87-05 | 栈溢出 ST(8) | 置 C1，不崩溃；或 #MF（按实现） |
| L2-X87-06 | 栈下溢 | 读 NaN indefinite |

### 4.2 x87 算术（T0/T1）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L2-X87-10 | `FADD`/`FSUB`/`FMUL`/`FDIV`（reg/mem） | 与 C `long double` 一致 |
| L2-X87-11 | `FADDP` 等 P 后缀 | 弹栈正确 |
| L2-X87-12 | `FIADD` 等整数变体 | 整数转 80 位后运算 |
| L2-X87-13 | 80 位精度 | `1/3` 结果与硬件一致（10 字节） |
| L2-X87-14 | `FCHS`/`FABS` | 符号位正确 |
| L2-X87-15 | `FXCH` | 交换 ST(0)/ST(i) |
| L2-X87-16 | `FCOM`/`FCOMP`/`FUCOM` | C0/C2/C3 正确 |
| L2-X87-17 | `FCOMI`/`FUCOMI` | 同时写 EFLAGS ZF/PF/CF |

### 4.3 x87 控制（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L2-X87-20 | `FNSTCW`/`FLDCW` | 控制字往返一致 |
| L2-X87-21 | `FNSTSW`/`FSTSW AX` | 状态字正确 |
| L2-X87-22 | `FNINIT` | 控制字 0x037F，栈空 |
| L2-X87-23 | 舍入模式 RC | 四种模式舍入方向正确 |
| L2-X87-24 | 精度控制 PC | 单/双/扩展精度 |

### 4.4 FXSAVE/FXRSTOR（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L2-FX-01 | 保存后恢复 | 全部寄存器一致 |
| L2-FX-02 | MXCSR 往返 | 一致 |
| L2-FX-03 | x87 tag word | 与栈状态一致 |
| L2-FX-04 | 512 字节对齐要求 | 非 16 字节对齐触发 #GP |
| L2-FX-05 | 与 XMM 交叉 | XMM0–15 全部保存/恢复 |

### 4.5 其他系统指令（T1）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L2-SYS-01 | `SWAPGS` | GS.base 与 KernelGSBase 交换 |
| L2-SYS-02 | `RDTSC` | EDX:EAX 单调递增 |
| L2-SYS-03 | `RDTSCP` | 额外返回 TSC_AUX |
| L2-SYS-04 | `LFENCE`/`SFENCE`/`MFENCE` | 空操作，不改变状态 |
| L2-SYS-05 | `PAUSE` | 空操作 |
| L2-SYS-06 | `BSWAP` | 字节序反转正确 |
| L2-SYS-07 | `CLFLUSH` | 空操作，不改变内存 |
| L2-SYS-08 | `LMSW`/`LLDT` | 状态更新正确 |
| L2-SYS-09 | CPUID 不暴露 APIC | leaf1 EDX bit9 = 0 |
| L2-SYS-10 | CPUID 暴露 TSC/FXSR | leaf1 EDX bit4/bit24 = 1 |

### 4.6 用户态浮点（T3）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L2-USR-01 | 静态程序做 double 运算 | 结果与宿主一致 |
| L2-USR-02 | `long double` 运算 | 80 位精度 |
| L2-USR-03 | SSE2 + x87 混合 | 互不干扰 |
| L2-USR-04 | 上下文切换后浮点保持 | 值不丢失 |

**L2 门禁**：L2-USR-04 通过 + ISA diff 全绿。

---

## 5. L3 — 内存权限与异常完备

### 5.1 分页权限（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L3-PG-01 | CPL3 读 supervisor 页 | #PF，err bit2(U)=1，bit0(P)=1 |
| L3-PG-02 | CPL3 写 supervisor 页 | #PF，err bit1(W)=1 |
| L3-PG-03 | CPL3 取指 supervisor 页 | #PF，err bit4(I/D)=1 |
| L3-PG-04 | CPL0 读只读页 | 允许 |
| L3-PG-05 | CPL0 写只读页（CR0.WP=1） | #PF |
| L3-PG-06 | CR0.WP=0 时 CPL0 写只读页 | 允许 |
| L3-PG-07 | 访问置 A 位 | 页表项 A=1 |
| L3-PG-08 | 写置 D 位 | 页表项 D=1 |
| L3-PG-09 | 保留位违例 | #PF，bit3(RSVD)=1 |
| L3-PG-10 | 非规范地址 | #GP(0) |
| L3-PG-11 | 缺页 err 的 P 位 | 页不存在时 bit0=0 |

### 5.2 段与特权（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L3-SEG-01 | 加载不存在选择子 | #NP 或 #GP(sel) |
| L3-SEG-02 | 加载空选择子到 CS | #GP(0) |
| L3-SEG-03 | CPL3 执行 `HLT` | #GP(0) |
| L3-SEG-04 | CPL3 执行 `LGDT`/`LIDT` | #GP(0) |
| L3-SEG-05 | CPL3 写 CR0/CR3 | #GP(0) |
| L3-SEG-06 | CPL3 执行 `WRMSR` | #GP(0) |
| L3-SEG-07 | `INT n` DPL 不足 | #GP((n<<3)\|2) |
| L3-SEG-08 | 段限长越界 | #GP 或 #SS(0) |

### 5.3 异常与 IST（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L3-EXC-01 | `UD2` | #UD(6)，无 err |
| L3-EXC-02 | `DIV 0` | #DE(0)，无 err |
| L3-EXC-03 | 除法溢出 | #DE(0) |
| L3-EXC-04 | `INT3` | #BP(3)，RIP 指向 INT3 之后 |
| L3-EXC-05 | `INTO` 且 OF=1 | #OF(4) |
| L3-EXC-06 | 双重故障 | #DF(8)，err=0，切 IST1 |
| L3-EXC-07 | 三重故障 | 复位或 halt（按实现） |
| L3-EXC-08 | NMI | vector 2，切 IST2（如配置） |
| L3-EXC-09 | err code 压栈顺序 | 先 err 后 RIP |

### 5.4 进程隔离（T3/T4）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L3-ISO-01 | 用户态写内核地址 | #PF + SIGSEGV |
| L3-ISO-02 | 用户态跳转内核地址 | #PF，err I/D=1 |
| L3-ISO-03 | 系统调用后返回用户态 | CPL=3 恢复 |
| L3-ISO-04 | 内核访问用户态指针 | 正确（U/S=1 且 CPL0） |

**L3 门禁**：L3-ISO-04 通过 + `make xemu-sys-test` ≥8 用例。

---

## 6. L4 — 稳定性与工具链

### 6.1 设备与 MSR（T0/T2）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L4-PCI-01 | 读 `0xCF8/0xCFC` | 无设备时返回全 1 |
| L4-PCI-02 | 写配置地址 | 无副作用 |
| L4-MSR-01 | EFER/STAR/LSTAR/SFMASK | 读写一致 |
| L4-MSR-02 | FS_BASE/GS_BASE | 读写一致 |
| L4-MSR-03 | TSC MSR | 与 `RDTSC` 一致 |
| L4-MSR-04 | 未实现 MSR 读 | #GP(0) |

### 6.2 调试工具（T0）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L4-DBG-01 | `itrace` 输出反汇编 | 与 llvm-objdump 一致 |
| L4-DBG-02 | `mtrace` 记录读写 | 地址/宽度/值正确 |
| L4-DBG-03 | `ftrace` 记录 call/ret | 调用深度正确 |
| L4-DBG-04 | ring buffer 16 条 | 崩溃时内容正确 |

### 6.3 长稳与性能（T5）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L4-PERF-01 | 吞吐 | ≥ 20 MIPS |
| L4-PERF-02 | 启动时间 | 到 `/init` < 60 秒 |
| L4-SOAK-01 | 连续运行 10 分钟 | 无 hang、无内存增长 |
| L4-SOAK-02 | 反复中断 | 计数单调，无丢失 |

### 6.4 端到端（T4）

| TC | 用例 | 期望 |
| --- | --- | --- |
| L4-E2E-01 | 进入 busybox shell | 出现提示符 |
| L4-E2E-02 | `ls /` | 列出 initramfs 内容 |
| L4-E2E-03 | `cat /proc/cpuinfo` | 输出 CPU 信息 |
| L4-E2E-04 | `echo hello` | 回显正确 |
| L4-E2E-05 | `dmesg \| tail` | 无 oops/panic |

**L4 门禁**：L4-E2E-05 通过 + 10 分钟长稳。

---

## 7. 回归门禁（每个里程碑都要跑）

| 门禁 | 命令 | 阈值 |
| --- | --- | --- |
| G1 解码 | `make decode-test` | 693/693 |
| G2 ISA 对拍 | `make isa-diff` | 0 mismatch |
| G3 用户态锁步 | `make emu-test` | 37/37 |
| G4 系统锁步 | `make sys-lockstep-test` | 10/10 |
| G5 系统 co-sim | `make sys-diff-test` | 10/10 |
| G6 XEMU 系统 | `make xemu-sys-test` | 通过 |
| G7 Stage-A 回归 | `make regress` | 37/37 |
| G8 Stage-B 回归 | `make ooo-test` | 29/29 |
| G9 单元测试 | `sbt test` | 21/21 |
| G10 fuzz | `make emu-fuzz` | 200/200 |

**规则**：任何门禁回归 → 阻断合并，优先修复。

---

## 8. 测试优先级与投入

| 里程碑 | 用例数 | P0（必须） | P1（重要） | P2（可选） |
| --- | --- | --- | --- | --- |
| L0 | 34 | 20 | 10 | 4 |
| L1 | 26 | 18 | 6 | 2 |
| L2 | 45 | 30 | 12 | 3 |
| L3 | 31 | 22 | 7 | 2 |
| L4 | 20 | 12 | 6 | 2 |
| **合计** | **156** | **102** | **41** | **13** |

---

## 9. 失败归档规范

失败用例统一落到 `tests/xemu_fail/<TC>.json`：

```json
{
  "tc": "L3-PG-05",
  "milestone": "L3",
  "seed": 12345,
  "image": "tests/system/t50_wp.S",
  "expected": {"vec": 14, "err": 0x3, "rip": "0x400123"},
  "actual":   {"vec": 14, "err": 0x1, "rip": "0x400123"},
  "note": "CR0.WP 未参与写权限判定"
}
```

要求：
- 必须可复现（含 seed 与镜像）
- 修复后化为**永久回归用例**
- 每个 P0 失败必须在 CHANGELOG 记录
