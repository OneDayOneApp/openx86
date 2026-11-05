
### 后续：补完剩余扫描缺口

修完 x87 后再扫，发现几处独立问题（都与"表项被字节截断"这一类有关）：

- **`0F AE` 组表被截断**：`g15[]` 是 `uint8_t`，而 `Ops_FXSAVE` 同批改动里挪到了
  **260**，于是 `260 & 0xFF = 4 = Ops_MOVSXD`——`FXSAVE` 静默解成 `MOVSXD`。
  所有 opcode 表（`alu`/`g15`/`k`/`t`/`btops`）统一改成 `uint16_t`。
  这是 `ops.h` 里"op 至少 16 位"注释警告过的同一类错误。
- **`0F C2` CMPPS/CMPPD/CMPSS/CMPSD**：未实现（busybox 用 `cmpltsd`）。按 8 个谓词
  实现，新增 `Ops_SIMD_CMP`。
- **`66 0F 71/72/73` 立即数移位组**：未实现（busybox 用 `psrlq`）。实现 PSRL/PSRA/
  PSLL 的字/双字/四字形式与 PSRLDQ/PSLLDQ，新增 `Ops_PSHIFT_IMM`。
- **`A0`–`A3` MOV moffs**：完全没有解码表项。新增地址宽度的立即数类别 `IMM_ADDR`
  （64 位模式下 8 字节，`0x67` 前缀下 4 字节，与数据宽度解耦）。
- **`0F 0D` PREFETCHW**、**`F2 0F 00 /6` LKGS（寄存器形式）**：补上。
- `isa_scan` 的判据 `op >= 0xff` 已过时（XEMU 私有指令现在从 300 起编号），
  导致 BSWAP/PAUSE 被误报；改为只看 `op == Ops_UNDEF || illegal`。

扫描结果：**busybox 245710 条指令 0 条缺失**；内核 3050325 条中剩 15 条，全部是
CPUID 未宣告的特性（`XSAVE`/`XRSTOR`/`INVPCID`/`TILERELEASE`），在 `/proc/cpuinfo`
的 flags 里确实没有，真机同样会 `#UD`——属于正确行为而非缺口。

## Unreleased — x87 寄存器形式（D9–DF）修复

`make xemu-isa-scan` 扫 busybox 时报出 `fchs`/`fabs` 缺失，追下去发现是一整片
x87 寄存器形式的解码错误，而且全都是**静默错误**（解成别的指令或 NOP，不报错）。

### 根因：三处叠加

1. **组号冲突**：`G_X87 = 8` 覆盖 D8..DF（8..15），而系统组 `G_15 = 9` 正好等于
   `G_X87 + 1 = D9`。于是 D9 逃逸被当成 `0F AE` 系统组处理，`x87_group()` 根本没
   被调用：`fld st(0)` 解成 **FXSAVE**（op 205），`fxch st(0)` 解成 **FXRSTOR**
   （op 206），`fchs`/`fabs`/`ftst`/`fxam` 直接 #UD。
   同时 `group_is_x87` 的边界写的是 `< G_15`，与 `G_15` 的取值耦合。

2. **reg/rm 语义整体写反**：寄存器形式里 **reg 选操作、rm 选 ST(i)**，代码却是
   `switch (rm)` 选操作、把 `reg` 当 ST 索引。DA..DF 的寄存器形式整片缺失，全部
   落到 `op = 0`（NOP）——`faddp`/`fmulp`/`fcompp`/`fcmov*` 静默变成空操作。

3. **ST 索引塞进 `.imm`**：`x87r()` 把 ST(i) 存进 `DInfo.imm`，而该字段是**立即数
   编码**，于是解码器给指令多算一个立即数字节（`d9c0` 的 `len` 从 2 变成 3）。

此外还发现四处执行层错误：

- **`P` 形式的目的是 ST(1) 不是 ST(0)**：`FADDP`/`FSUBP`/`FMULP`/`FDIVP` 把结果
  写进 ST(0) 再弹出，等于每次操作静默丢掉一个栈项。
- **`FSUBP`/`FSUBRP`/`FDIVP`/`FDIVRP` 操作数顺序**：SDM 规定非反向形式算
  `DEST - SRC`（DEST 是 ST(i)），反向形式算 `SRC - DEST`；原实现混用了 `x`/`y`。
- **`FNSTSW` 不输出 TOP**：状态字 bit 13:11 是 TOP，原实现只返回 `fpu.sw`，
  推栈/弹栈后 TOP 恒为 0。
- **`FXAM` 分类位错误**：`+0` 应给出 C3=1，原实现全 0；`FNSTSW AX` 的寄存器
  判定写成 `reg == 7 && rm == 4`（DF E0 实际是 `reg 4, rm 0`），从未命中。

### 顺带补齐的指令

此前缺失、现按 SDM 实现：`FCMOVB/E/BE/U` 与 `FCMOVNB/NE/NBE/NU`、`FCOMI`/
`FUCOMI`/`FCOMIP`/`FUCOMIP`（写 EFLAGS）、`F2XM1`/`FYL2X`/`FYL2XP1`/`FPATAN`/
`FPTAN`/`FSIN`/`FCOS`/`FSINCOS`/`FRNDINT`/`FSCALE`/`FPREM`/`FPREM1`/`FXTRACT`、
`FISTTP`、`FDECSTP`/`FINCSTP`、`FNSTSW AX`。

### 编号与派发

x87 派发用的是区间判断 `op >= Ops_FNINIT && op < Ops_FXSAVE`。新指令必须落在
该区间内，因此 `Ops_FXSAVE` 显式钉到 **260**，给 x87 留出 160..205 的连续空间；
`decode.c` 加了 `_Static_assert` 防止将来重编号把 x87 挤出区间。

### 测试

- 新增 `tests/system/t86_x87_regform.S`：16 项检查，每项把 ST(0) 存成 IEEE
  double 后**按位比较**期望值，覆盖 FLD/FXCH/FCHS/FABS/FSQRT/FTST/FXAM 与四个
  `P` 形式、`FCOMPP`、`FFREE`、`FNSTSW AX`。
- 测试用**显式字节**而不是助记符：GNU `as` 把 `fsubp`/`fsubrp` 和
  `fdivp`/`fdivrp` 的助记符与 SDM 编码**对调**（`fsubp` 汇编成 `DE E1`，而
  SDM 里 `DE E1` 是 `FSUBRP`）。XEMU 跟 SDM/Bochs 表走，不跟 GAS 助记符。

## Unreleased — L4 完成：PCI 桩、诊断工具链、性能 ≥20 MIPS

### L4 收尾

- **PCI 配置空间桩**（`xemu/dev/pci.c`）：0xCF8/0xCFC mechanism #1，
  字节/字/双字与未对齐访问、缺设备读全 1、最小 440FX host bridge
  （bus0/dev0/fn0，只读 ID 寄存器忽略写入）。测试 `t85_pci`。
- **诊断工具链**：`tools/xemu_itrace.py` 提供 NEMU 风格的
  `itrace`/`ftrace`/`hot-pc`/`stats`，配合 `--trace-pc`、新增的 `--trace-bb`
  与 `--trace-ring`，用 `vmlinux` 符号还原调用树与热点函数。
- **稳定性门禁**：`make xemu-soak-test`（`tools/linux/soak_test.py`）连续
  10 分钟、每 8 秒敲一条命令（`echo`/`ls`/`cat /proc/*`/`date`/`pidof`），
  任何一条超时无响应或内核报 BUG/oops 即失败。实测 **10.4 分钟 / 72 条命令
  全部通过**。
- 新增 MSR：`IA32_PAT`（复位值 `0x0007040600070406`）、`IA32_MTRR_DEF_TYPE`、
  `IA32_DEBUGCTL`；MTRRCAP 与 MTRRphysBase/Mask 读 0（CPUID 未声明 MTRR）。

### 性能：7.75 → 22 MIPS（2.8 倍）

采样定位到 `xemu_decode()` 占启动过程 **69%** 的运行时间——内核反复执行同
几千条指令，而每次解码工作完全相同。三项改动：

1. **解码缓存**（`xemu/decode.c`）：以 PC 为键的直接映射表，命中即拷贝
   `XemuInst`。正确性靠 `xemu_code_gen`：写入"曾经存放过代码"的物理页、
   CR3 装载、INVLPG 都会递增该代号，使旧条目失效。代码页用位图标记，
   普通数据写入只多一次位测试。
2. **访存快路径**（`xemu/mem.c`）：单页内 ≤8 字节的访问直接走内联翻译，
   跳过循环、分页搬运与设备分发；权限判定复用 `tlb_allows()`/`tlb_set_dirty()`
   本身，**不复制规则**（第一版复制了一份并在 CR0.WP 与 A/D 上出错，
   被 `t71`/`t72` 当场抓住）。
3. **每指令设备钩子短路**（`xemu/dev/pit.c`、`pic.c`）：PIT 在累计时钟不足
   一个 tick 时直接返回；PIC 在 IRR∧¬IMR 为空时直接返回 -1。两者都是
   "每指令调用一次"的函数，空路径必须是几个整数操作。

`LD`/`ST` 的值拼装也改成按宽度分支，省掉逐字节移位循环（A/B 测得约 4%）。

实测（30M / 100M 指令，CPU 时间）：**3.87 s → 2.07 s / 4.54 s ⇒ ~22 MIPS**，
达到计划 L4 的 ≥20 MIPS 目标。

## Unreleased — CPUID 按 PRD 对齐：vendor `OpenX86-Core`

`/proc/cpuinfo` 之前显示 `vendor_id : X86-KMH-Core`，与产品规范
`docs/PRD.md §5.7` 规定的 `OpenX86-Core` 不一致。

- **CPUID.0**：EAX=1，EBX/EDX/ECX = `Open` / `X86-` / `Core`。
  RTL（`src/main/scala/x86/core/X86Core.scala`）同步改成同一字符串，
  `t29_cpuid` 的断言随之更新，系统逐指令对拍重新通过。
- **CPUID.1**：EDX = TSC MSR PAE CX8 SEP CMOV CLFSH FXSR SSE SSE2。
  为了能"如实声明 SEP"，顺带**实现了 `SYSENTER`/`SYSEXIT`**
  （`0F 34`/`0F 35` + MSR `0x174/0x175/0x176`，含 `CR4.SEP` 的 #UD 检查）。
- **CPUID.80000000h** = 0x80000008；**80000001h** EDX = SYSCALL NX RDTSCP LM、
  ECX.LAHF_LM=1；**80000008h** = 物理 40 位 / 线性 48 位（`XEMU_MAXPHYADDR`
  同步改为 40，驱动保留位检查）。
- 唯一有意保留的偏离：PRD 表格写 "FPU/MMX 置 0"，但 Linux 在
  `CPUID.1:EDX.FPU=0` 时直接 `x86/fpu: Giving up, no FPU found` panic，
  而 XEMU 确实实现了 x87/FXSAVE 子集，因此 **FPU 置 1、MMX 置 0**，并在
  代码与测试里注明原因。

新增系统测试：

| 测试 | 覆盖 |
| --- | --- |
| `t78_cpuid`（0x78） | vendor 字符串、leaf 1 / 80000000h / 80000001h / 80000008h 逐位校验 |
| `t79_sysenter`（0x79） | SYSENTER→CPL0（CS/SS/RSP/IF）、SYSEXIT→CPL3（RCX/RDX）往返 |

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
