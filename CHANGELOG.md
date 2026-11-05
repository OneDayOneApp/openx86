## Unreleased — fork/COW、SSE 逻辑运算、串口输入不再阻塞

### 修复

- **同值 CR3 重载必须刷新 TLB**：x86 规定写 CR3 即刷新 TLB（即使值不变），
  Linux 依赖这一点 —— `dup_mmap()` 把父进程 COW 页写保护后调用
  `flush_tlb_mm(oldmm)`，最终执行 `native_write_cr3(__native_read_cr3())`。
  XEMU 只在 CR3 值变化时刷新，父进程于是继续用可写的陈旧翻译直接写进与
  子进程共享的物理页；子进程 COW 拷贝到被污染的栈，busybox fork 包装的
  `ret` 弹出 1 → `segfault at 1 ip 1`。现在任何 CR3 写都刷新 TLB。
  新回归 `t76_cr3_flush`（0x76）。
- **SSE 逻辑运算与字重排**：`andps/andnps/orps/xorps`（0F 54–57）与
  `pshufhw/pshuflw`（F3/F2 0F 70）原先能解码但 sse.c 没有实现，任何用到
  它们的程序都会 `unsupported op=8x`（busybox `ls` 的第一条路径就是
  `xorps %xmm0,%xmm0`）。同时修掉 `pshufh[lw]` 写 `Vec128.w[4..7]` 的越界。
- **串口输入不再阻塞**：`xemu_uart_poll()` 一次性把 stdin 设为非阻塞。
  原先一旦客户机打开接收中断而宿主没有输入，整个模拟器就阻塞在 `read(2)`，
  表现为启动到一半"卡住"（banner 永远不出现）。

### 交互式 shell 实测

```
/ # ls /bin
[         date      halt      mkdir     pwd       sort      uname
ash       df        head      mknod     reboot    sync      vi
busybox   echo      kill      mount     sed       test      whoami
cat       false     less      mv        setsid    touch
chmod     free      ln        poweroff  sh        true
cp        grep      ls        ps        sleep     umount
/ # poweroff -f
reboot: System halted
```

`make xemu-shell-test`：banner → `/ #` 提示符 → `echo XEMU-ECHO-OK` 输出 →
`poweroff -f` 关机，**PASS**（约 15 s）。

- **间接 `JMP` 的故障原子性**：`xemu.rip = read_op(d,1,8)` 在操作数取数
  缺页时会把 `xemu_raise_exc()` 刚装好的处理程序 RIP 覆盖成 `ld()` 返回的 0
  —— 于是 CPU 以处理程序 CS 去取线性地址 0，且因为真正的处理程序没跑、
  `SWAPGS` 没执行，GS 还是用户基址，oops 就表现为
  `RIP=0 / RSP=入口 trampoline 栈 / GS=0`。
  busybox 的 `jmpq *0x4e62d0(,%rax,8)` 跳表页正好是首次按需分页，
  所以管道场景必现。`CALL`/`RET`/`IRET` 早已检查 `fault_pending`，只漏了
  `JMP`。新回归 `t77_jmp_fault`（0x77）。

### 交互式 shell 实测（完整 L4 验收）

```
XEMU-SHELL-READY
/ # echo XEMU-ECHO-OK
XEMU-ECHO-OK
/ # ls /bin
[         date      halt      mkdir     pwd       sort      uname
ash       df        head      mknod     reboot    sync      vi
...
/ # cat /proc/cpuinfo | head -3
processor       : 0
vendor_id       : X86-KMH-Core
cpu family      : 6
/ # poweroff -f
reboot: System halted
```

`make xemu-shell-test` 覆盖：banner → `/ #` → `echo` → `ls /` →
`cat /proc/cpuinfo` → `cat /proc/cpuinfo | head -3`（两个 fork 出来的 applet
之间的管道）→ `poweroff -f`，**PASS**（约 11 s）。

### 已知限制

- ash 在真实 tty 上会发 `ESC[6n` 光标查询并等待终端应答；真实终端会自动
  应答，纯管道 harness 需要在看到查询后回 `ESC[1;1R`（`shell_test.py` 已实现）。
- 串口 FIFO 只有 1 字节，输入速率受 `xemu_uart_poll` 每 1024 条指令一次限制。

## Unreleased — L4：交互式 busybox shell

在最小 Linux 6.6.60 + busybox（static musl）initramfs 下，XEMU 现在能跑到
**可交互的串口 shell**：

```
XEMU-SHELL-READY
sh: can't access tty; job control turned off
/ # echo XEMU-ECHO-OK
XEMU-ECHO-OK
/ # poweroff -f
reboot: System halted
```

### 修复（让 shell 能起来的三个关键缺陷）

- **16550 THRE 发送中断**：THR 写完后置 THRE、按 IER 拉 IRQ4，读 IIR 清除。
  没有它，tty 层写完 1 字节就等中断，用户态 `write(2)` 只剩第一个字节。
- **页表保留位掩码**：4 级分页 + `CR4.PKE=0` 时 bits 62:52 是软件可用位，
  Linux 把 `_PAGE_SAVED_DIRTY` 放在 bit 58、`_PAGE_DEVMAP` 放在 bit 57。
  原先 `62:51` 全判保留，导致每次 fork/COW 写保护用户 PTE 都
  `Corrupted page table` → `page_fault_oops`。
  现在只保留 MAXPHYADDR 之上的物理地址位（CPUID.80000008h 报告 phys=36）。
- **指令级故障原子性**：`call`/`ret` 的栈访问缺页时，硬件要求指令完全未执行
  （帧里 RIP=该指令、RSP=进入时的值）。原先在 `st()` 里立刻投递异常、随后
  `Ops_CALL` 又写 `xemu.rip = target`，覆盖了刚装好的处理程序 RIP，返回时
  落到被调用者中间且 RSP 在异常栈上。现在记录 `inst_entry_rsp`、缺页时回滚
  RSP，`CALL`/`RET` 在缺页时不再提交 RIP。

### 新增指令 / 解码

- `9B`（FWAIT）解码到已有的 no-op `Ops_FWAIT`：进程退出时
  `fpu__drop` 会执行它，原先直接 "unsupported op=255"。

### 诊断

- `--stop-at` 改为在解码**之前**判断，因此可以停在本身取指就会缺页的地址
  （例如被踩坏的跳转目标），并在该路径上也打印 PC 环形缓冲。
- 新增 `--watch-va=ADDR`：**虚拟地址**写观察点（过 MMU），可配合
  `--watch-byte=XX` 只匹配写入的值。用来回答"是谁把 1 写进这个用户栈槽"。

### 已知问题

- `fork` 出来的子进程会在 exec 前 `segfault at 1 ip 1`（用户态 `ret` 从栈上
  弹出 1），因此 `mount`/`ls`/`cat` 这类非内建命令还跑不了；父 shell 不受影响。
  正在定位。

## Unreleased — L1 完成：真实 Linux 引导到用户态 `/init`

`make xemu-linux-test` 首次全绿：Linux 6.6.60 从 64 位引导协议进入
`startup_64`，约 2740 万条指令后执行 `Run /init as init process`，
用户态 `/init` 完成 `write`/`reboot` 并 `reboot: System halted`。
约 4.2 MIPS（30M 指令 ≈ 7 s）。

### 修复（12 个把内核打死的缺陷）

- 解码组表 `groups[G_15-1]` 越界写坏相邻 `.bss`（x87 状态就排在其后）
- 异常帧改用**虚拟地址**压栈（原先用物理写，开页后高地址内核栈帧丢失）
- 异常帧 CS/SS 用真实选择子（原先硬编码 SS=0/CS=8，Linux 认成
  `__KERNEL32_CS` 走 32 位返回路径）
- `MOV r/m16, Sreg`（`8C`）方向写反，`movl %cs,%eax` 变成空操作
- 实现 **AH/CH/DH/BH** 高字节寄存器（原先一律别名到低字节，
  `testb $0x10,%ah` 读错使 LA57 判断走错分支、printk 宽度错乱）
- 4KB PTE 屏蔽 **NX 位**（原先 `e & ~0xfff` 把 bit63 折进物理地址，
  一开 NX 所有数据页不可读）
- **BT/BTS/BTR/BTC 内存操作数**按位偏移前进有效地址（Bochs 语义）
- `0F AE` 不再被当成 x87 转义组（FXSAVE/FXRSTOR/CLFLUSH/MXCSR 全部解码成垃圾）
- `#PF` 错误码补 **U/S 位**（用户缺页被当作内核缺页 → `page_fault_oops`）
- GDT 描述符取满 **64 位 base**（TSS 在 CPU-entry area，低 32 位截断成 0x3000）
- TSS `rsp0` 不再截成 32 位
- 8259A ICW1 不再清 IMR（ICW 序列期间 IRQ0 被投递到未安装的 IDT 项）

### 设备

- 16550 **THRE 发送中断**（IIR=0x02 + IRQ4，读 IIR 清除）：补上后
  用户态 `write(2)` 才不会被 tty 层卡在第一个字节
- 8259A OCW2 特指 EOI 的 level 取 bits 2:0

### 新增指令

`SGDT SIDT SMSW LMSW SLDT STR LLDT VERR VERW SWAPGS RDTSC RDTSCP RDPMC
CLTS PAUSE BSWAP XGETBV CLAC STAC SHLD SHRD POPCNT MOVNTI PREFETCH WBINVD
EMMS FWAIT RDRAND RDSEED LAR XSETBV XSAVES XSAVEC XRSTORS LKGS MONITOR
MWAIT TILERELEASE RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE UD1`

### 工具

- `xemu --trace-exc/--stop-at-exc/--trace-pc/--dump-vmem`
- `xemu --watch-mem/--watch-byte/--watch-after` 物理写观察点
- `xemu --serial-stop=STR`
- `tools/isa_scan.py` + `xemu/isa_scan.c`：静态扫描内核镜像的未实现指令
- `tools/linux/build_kernel.sh` / `mkinitramfs.py`：Docker 交叉编译最小内核
- `tools/bochs/{boot_kernel.S,kernel_boot.py,trace_compare.py}`：Bochs 逐指令仲裁
- `tools/linux/boot_test.py` + `make xemu-linux-test`

### 测试

- `t63_bt_mem`（内存位操作跨元素）、`t64_sgdt_sidt`（64 位伪描述符）
- `isa-diff` 从 14300 → **18600** 向量：高字节寄存器、SHLD/SHRD、POPCNT、
  BSWAP、MOVNTI、内存位操作
- 解码字段测试：立即数操作数的寄存器下标按 don't-care 处理

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
