# BigOS

语言：[English](README.md) | 简体中文

BigOS 是一个早期阶段的 x86_64 操作系统内核，主要使用 freestanding
C++17、C17 和汇编编写。它已从 boot/kernel 骨架迭代为具备最小用户态闭环的单核内核：
引导流程、文本/串口输出、中断/异常/syscall 处理、PIT timer tick、键盘驱动的
TTY 输入路径、协作式内核线程调度器、`int 0x80` syscall 入口、默认关闭的首个
ring3 用户程序，以及一套相对完整的早期内核内存管理。

本仓库是一个研究/玩具操作系统内核项目，不是托管应用或服务。

## 状态

项目已从内核基础设施引导阶段，迭代为在 boot 路径、中断基础设施和早期内存管理之上，
具备 timer、输入、调度、syscall 和最小用户态 smoke 的单核内核。

已经实现或部分实现：

- x86 引导路径，包括 MBR、exFAT DBR、扩展 DBR 和长模式切换。
- 从 exFAT 磁盘镜像加载 ELF64 内核。
- 高半区内核链接地址 `0xffffffff80000000`。
- VGA 文本模式输出、`kprintf`，以及用于确定性标记的 COM1 串口输出。
- kernel-owned 静态 IDT、生成的汇编 ISR 桩，以及把 CPU exception、i8259 IRQ
  与 `int 0x80` syscall 向量分离的稳定 `InterruptFrame` dispatch ABI。
- 诊断型 `#PF` 处理器：读取 `CR2` 并输出 `BIGOS_PAGE_FAULT` 标记。
- 统一的早期致命诊断（`bigos::kpanic`/`khalt`）：在 COM1 与 VGA 输出稳定的
  `BIGOS_PANIC code=<code> source=<source>` 标记，然后关中断并停机。
- i8259 PIC 驱动，以及 IRQ0 上的 PIT timer 驱动，提供单调 tick 和最小 `mdelay`
  忙等原语。
- 键盘 IRQ1 scancode 解码（US Set 1）接入定容 TTY/console 输入路径；console
  输出走 VGA。
- 协作式（非抢占）单核内核线程调度器：1 页内核栈、x86_64 context switch、
  round-robin `yield()` 和 idle 线程；timer IRQ 仅记录 bounded reschedule intent。
- `int 0x80` syscall 入口、最小寄存器 ABI 和诊断 dispatcher
  （`SYS_DEBUG_WRITE`、`SYS_GET_TICK`、`SYS_WRITE`、`SYS_EXIT`）。
- 默认关闭的首个 ring3 用户程序 smoke：flat embedded image 通过 TSS/RSP0 与
  `iretq` 进入 ring3，并完成 `SYS_WRITE`/`SYS_EXIT` 闭环。
- 基于 buddy 的物理页分配器，并使用 early metadata arena 完成 bootstrap。
- Slab/kmalloc 分配器：size class、动态 slab 回收、page-backed 大对象分配、
  可选 debug guard 和验证统计。
- 内核虚拟内存分配器（first-fit、四级页表映射、释放时清除 PTE 并刷新 TLB）、
  内核 direct map，以及 C++ `new`/`delete` 集成。
- 显式分配 API：内核虚拟页使用 `alloc_kernel_pages(nr_pages, flags)`，
  物理 buddy 使用内部 `alloc_physical_order(order, flags)`。
- 可切换的早期内存运行时自检（`bigos::mm::self_test`）。
- 小型 KTL 支持库，用于内核容器和辅助工具。

尚未实现或仍处于骨架状态：

- UEFI bootloader、ESP 镜像生成和 OVMF/QEMU UEFI smoke test。
- 抢占调度、优先级、时间片、sleep queue 和阻塞语义。
- 完整多进程模型：多进程调度、fork/exec、信号和进程回收（目前仅有首个用户程序 smoke）。
- ELF 用户程序加载（smoke 使用 flat embedded image）。
- demand paging、copy-on-write、`mmap`/`brk` 和空页表回收。
- 内核内的文件系统与块设备服务。
- 更广泛的设备驱动支持。
- 完整的构建/安装自动化与 CI。

## 仓库结构

```text
.
|-- cpp               内核 C++ 支持库、KTL、libsupc++ 子集
|-- include           公共内核头文件和小型 libc 风格头文件子集
|-- src               boot、kernel、drivers、mm、runtime 等实现源码
|   |-- arch/x86/boot x86 引导代码、MBR/DBR 和 ELF 加载器
|   |-- drivers       VGA、i8259 PIC、PIT timer 等硬件驱动
|   |-- kernel        内核入口及子系统：irq、timer、terminal（console/
|   |                 keyboard/tty）、sched、syscall、proc、底层 IO
|   |-- mm            buddy、slab、kmalloc、虚拟内存和 direct map 代码
|   `-- runtime       运行时启动汇编源码对象
|-- tools             boot 磁盘安装工具等开发辅助脚本
|-- openspec          OpenSpec 项目配置
|-- tests             验证测试和本地测试资产
|-- link.lds          内核链接脚本
|-- toolchains.lua    xmake 交叉工具链定义
|-- xmake.lua         主构建配置
`-- bigos.py          辅助命令工具
```

## 引导流程

```text
BIOS
  |
  v
MBR / exFAT DBR
  |
  v
boot.s
  - 构建临时 GDT
  - 准备早期页表
  - 启用 PAE 和长模式
  - 切换到 64 位执行
  |
  v
boot.cc
  - 在 exFAT 根目录中查找名为 "kernel" 的文件
  - 通过 ATA LBA48 PIO 读取 ELF64 头和程序头
  - 为内核镜像准备分页
  - 将内核加载到高半区虚拟地址
  |
  v
kernel()
  - 清空 VGA 文本屏幕
  - 初始化串口输出（serial_init）
  - 初始化内存管理（init_mem）
  - 可选运行早期内存运行时自检（mm::self_test）和用户地址空间 vmem smoke
  - 初始化 TTY/console 输入路径（terminal::init_tty）
  - 初始化 kernel-owned IDT、异常/IRQ/syscall 分发、i8259 PIC、IRQ0 上的 PIT
    timer 和 keyboard IRQ1 路径
  - 在 early handler 注册完成后开启中断
  - 在串口与 VGA 输出 "BigOS kernel reached" 标记
  - 可选运行 syscall、scheduler 和首个用户程序 smoke
  - 通过 sched::start() 进入协作式调度器；停机行为由调度器 idle 线程拥有，
    取代裸 hlt 尾循环
```

关键文件：

- `src/arch/x86/boot/boot.s`：早期 CPU 模式切换并跳转到内核。
- `src/arch/x86/boot/boot.cc`：磁盘读取、exFAT 查找、ELF 加载。
- `src/kernel/kernel.cc`：主内核入口。
- `link.lds`：高半区内核布局。
- `docs/zh/arch/x86-boot-layout.md`：当前 Legacy BIOS 地址和 handoff 布局。
- `docs/zh/arch/uefi-boot-blueprint.md`：未来 UEFI 兼容蓝图；这是项目规划，
  不是当前可运行的启动路径。

## 构建与运行

主要构建系统是 xmake，预期编译器是 `x86_64-elf-gcc`。

```bash
xmake
```

可选验证构建开关（全部默认关闭；见 `xmake.lua`）：

```bash
xmake f --mm_self_test=y      # BIGOS_MM_SELF_TEST -> BIGOS_MM_SELF_TEST_PASSED/FAILED
xmake f --slab_debug=y        # BIGOS_SLAB_DEBUG slab debug guard（被 mm_self_test 隐含启用）
xmake f --page_fault_smoke=y  # BIGOS_PAGE_FAULT_SMOKE -> BIGOS_PAGE_FAULT
xmake f --timer_smoke=y       # BIGOS_TIMER_SMOKE -> BIGOS_TIMER_IRQ
xmake f --keyboard_smoke=y    # BIGOS_KEYBOARD_SMOKE 启用 IRQ1 线
xmake f --scheduler_smoke=y   # BIGOS_SCHEDULER_SMOKE -> BIGOS_SCHED_THREAD_A/B
xmake f --user_vmem_smoke=y   # BIGOS_USER_VMEM_SMOKE -> BIGOS_USER_VMEM_SMOKE_PASSED/FAILED
xmake f --syscall_smoke=y     # BIGOS_SYSCALL_SMOKE -> BIGOS_SYSCALL_SMOKE_PASSED/FAILED
xmake f --user_program_smoke=y # BIGOS_USER_PROGRAM_SMOKE -> BIGOS_USER_ENTER/EXIT
```

`--mm_self_test` 会自动启用 `--slab_debug`。自检会在 COM1 与 VGA 输出
`BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` 标记。
`--user_program_smoke` 会额外编译 `src/kernel/proc/**` 并进入首个 ring3 用户程序，
默认不参与普通启动。所有标记都写到 COM1 串口和 VGA。

当前 Legacy BIOS/MBR/exFAT 路径的一行本地启动调试：

```bash
make boot-debug
```

该命令是下面 Python 主入口的薄包装：

```bash
python3 tools/boot_debug.py run
```

命令会依次执行 preflight 检查、`xmake` 内核构建、`make -C
src/arch/x86/boot build-mbr build-dbr build-exdbr build-boot` boot 产物构建、
完全用户态 raw 磁盘镜像生成、MBR/exFAT boot region/`/boot/boot.bin`/根目录
`kernel` 写入，然后启动 Bochs。它不会构建 UEFI loader、ESP 镜像或 OVMF 配置。

默认生成物均位于 `build/` 下：

- raw 磁盘镜像：`build/test/os.raw`。
- 生成的 Bochs 配置：`build/test/bochsrc.bxrc`。
- boot 产物：`build/bin/x86/boot/`。
- 内核 ELF：`build/kernel`。

常用参数示例：

```bash
python3 tools/boot_debug.py run --image build/test/debug.raw --image-size 128M
python3 tools/boot_debug.py run --no-launch
python3 tools/boot_debug.py run --romimage /path/to/BIOS-bochs-latest --vgaromimage /path/to/VGABIOS-lgpl-latest
python3 tools/boot_debug.py run --memory-self-test --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED
python3 tools/boot_debug.py run --user-program-smoke
python3 tools/boot_debug.py validate-image --image build/test/os.raw
```

`--memory-self-test` 会以 `BIGOS_MM_SELF_TEST` 构建并把 COM1 路由到串口日志；
配合 `--expect-serial-marker`/`--smoke-timeout` 可执行有界 smoke test。
`--user-program-smoke` 会以 `BIGOS_USER_PROGRAM_SMOKE` 构建并进入首个 ring3
用户程序。按项目工具约定，Python 辅助脚本应通过 `uv run` 运行，例如
`uv run python tools/boot_debug.py run --no-launch`。

也提供 GUI 快捷命令，包括启用首个用户程序 smoke 的变体：

```bash
make boot-debug-gui
make boot-debug-user-gui
```

raw image 由 Python 标准库直接写入生成，不依赖 macOS `diskutil`、Linux loop
device、挂载权限、`mkfs.exfat` 或手工准备的 exFAT 镜像。

第一阶段范围：

- 该流程仅支持 Bochs。
- `make boot-debug` 保持 Legacy BIOS 调试入口语义。未来 UEFI 工作流规划为
  `make uefi-boot-debug` 等独立命令，使用隔离的 ESP/FAT 镜像产物，并以
  QEMU + OVMF 作为首选 smoke test 路径。
- QEMU/headless、串口日志自动判定和 CI smoke test 留给后续阶段。
- 该流程不修改 `boot.s`、`boot.cc`、`BootInfo`、`link.lds`、高半区内核地址或
  内核运行时初始化顺序。

常见失败原因：

- 缺少 `xmake`、`bochs`、`python3` 或 `x86_64-elf-*` 工具时，会在 preflight
  阶段失败，且不会生成或覆盖镜像。
- 内核或 boot 构建失败时会停止流程，不会继续使用 stale 产物启动。
- 如果 Bochs 安装需要宿主机特定 BIOS/VGA BIOS 路径，可以使用 `--romimage`、
  `--vgaromimage`、`--bochsrc` 或 `--bochs-extra` 指定。
- 历史 `test/bochsrc.bxrc` 只能作为宿主机配置参考；生成的
  `build/test/bochsrc.bxrc` 默认不会硬编码 Windows 路径、`win32` display
  设置或固定 ROM 路径。

使用已有 Bochs 配置运行：

```bash
xmake run kernel
```

顶层 `Makefile` 也提供了 Bochs 运行快捷命令：

```bash
make run
```

注意事项：

- 构建前请安装或暴露 `x86_64-elf-gcc`、`x86_64-elf-g++`、
  `x86_64-elf-ld` 以及相关 binutils。
- 在模拟器中运行内核前请安装 Bochs。
- `test/bochsrc.bxrc` 可能包含与主机相关的路径。请根据本机环境更新
  BIOS、VGA BIOS 和磁盘镜像路径。
- `bigos.py` 中的一些 Python 辅助命令仍是占位内容，不应视为完整自动化。

## 架构概览

```text
               +---------------------+
               |      boot code      |
               | MBR/DBR/boot.s/.cc  |
               +----------+----------+
                          |
                          v
               +---------------------+
               |     kernel entry    |
               |     kernel.cc       |
               +----------+----------+
                          |
          +---------------+----------------+
          |               |                |
          v               v                v
   +-------------+  +-------------+  +-------------+
   | memory (mm) |  | interrupts  |  |  drivers    |
   | buddy/slab  |  | IDT/ISR/PIC |  | VGA/i8259   |
   +------+------+  +------+------+  +------+------+
          |                |                |
          +----------------+----------------+
                          |
                          v
               +---------------------+
               | kernel C++ support  |
               | KTL/new/delete/ABI  |
               +---------------------+
```

## 主要子系统

### 引导

引导加载器专用于 x86/x86_64，并假设磁盘镜像布局中存在一个 exFAT 分区，
该分区包含名为 `kernel` 的文件。
当前可运行 backend 是 Legacy BIOS；UEFI 作为未来并行 backend 记录在
`docs/zh/arch/uefi-boot-blueprint.md` 中。

- `src/arch/x86/boot/mbr.s`：第一阶段引导代码。
- `src/arch/x86/boot/dbr_exfat.s`：exFAT 引导扇区代码。
- `src/arch/x86/boot/exdbr_exfat.s`：扩展 exFAT 引导代码。
- `src/arch/x86/boot/boot.s`：模式切换、早期页表、转入长模式。
- `src/arch/x86/boot/boot.cc`：ATA 磁盘读取、exFAT 目录扫描、ELF64 加载。
- `tools/install.py`：用于将引导扇区写入虚拟磁盘镜像的辅助工具。

### 内核入口

`src/kernel/kernel.cc` 当前执行的运行时设置：

- 清空 VGA 文本屏幕并初始化 COM1 串口输出。
- 调用 `bigos::init_mem()`。
- 可选运行 `bigos::mm::self_test()`（以 `BIGOS_MM_SELF_TEST` 构建时）和用户
  地址空间 vmem smoke（`BIGOS_USER_VMEM_SMOKE`）。
- 初始化 TTY/console 输入路径（`bigos::terminal::init_tty()`）。
- 调用 `bigos::irq::initIRQ()`（IDT、异常/IRQ/syscall 分发、PIC、IRQ0 上的 PIT
  timer、keyboard IRQ1）。
- 可选触发 page-fault smoke（以 `BIGOS_PAGE_FAULT_SMOKE` 构建时）。
- 开启中断并输出 "BigOS kernel reached" 标记。
- 可选运行 syscall、scheduler 和首个用户程序 smoke。
- 通过 `bigos::sched::start()` 进入协作式调度器；停机行为由 idle 线程拥有。

### 内存管理

内存子系统位于 `src/mm/`，是内核中最完善的部分。公开 API 通过命名区分分配层级：
内核虚拟页使用页数语义 `alloc_kernel_pages(nr_pages, flags)`，内部物理分配使用
buddy order 语义 `alloc_physical_order(order, flags)`，旧的混合语义 alias 已移除。

- `src/mm/buddy.cc` / `src/mm/buddy.h`：解析 BIOS 内存映射，划分 DMA/DMA32/NORMAL
  区域，管理物理页块，并使用 early metadata arena 完成 bootstrap，使初始化不依赖
  动态 slab 扩容。
- `src/mm/slab.cc` / `src/mm/slab.h`：size class 缓存、`kmalloc/free`、动态 slab
  回收、page-backed 大对象分配、可选 debug guard 和统计。
- `src/mm/kmem.cc` / `src/mm/kmem.h`：kmalloc/free 集成与 cache 接线。
- `src/mm/vmem.cc` / `src/mm/vmem.h`：first-fit 内核虚拟区间、四级页表映射、
  释放时清除 PTE 并刷新 TLB。
- `src/mm/memory.cc`：公开内存 API 入口。
- `src/mm/self_test.cc`：可切换的早期运行时自检，输出确定性
  `BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` 标记。
- `include/bigos/memory.h`：暴露公共内存分配 API。
- `src/mm/memdef.h`：定义 mm 私有的页大小、buddy 阶数和分配标志。

自检用法见 `docs/zh/arch/memory-runtime-validation.md`。

### 中断与输入

中断子系统结合了汇编桩和 C++ 描述符。内核在开启可屏蔽中断前加载 kernel-owned
静态 IDT，并通过稳定的 `InterruptFrame` dispatch ABI 路由所有 ISR 入口。

- `src/kernel/irq/interrupt.s`：生成的 ISR 入口桩。
- `src/kernel/irq/interrupt.cc`：IDT 初始化、异常分发、外部 IRQ 分发，以及
  `int 0x80` syscall 向量路由。
- `src/drivers/irqchip/i8259.cc`：PIC remap、屏蔽和 EOI 支持。
- `src/kernel/irq/isr.cc`：诊断型 `#PF` 处理器、timer IRQ0 tick 钩子和
  keyboard IRQ1 处理器。
- `include/irq/interrupt.h`：描述符布局、`InterruptFrame` 和向量常量。
- `docs/zh/arch/interrupt-exception-foundation.md`：当前中断/异常设计、非目标和验证记录。

CPU exception（`0x00`-`0x1f`）、remap 后的 i8259 IRQ（`0x20`-`0x2f`）和 syscall
向量（`0x80`）分开分发，只有外部 IRQ 才发送 EOI。syscall IDT gate 提升到 DPL=3
以便 ring3 触发 `int 0x80`，exception 和外部 IRQ gate 仍保持 ring0-only。

### Timer

PIT 在 IRQ0 上驱动周期性 tick。

- `src/drivers/timer/pit.cc` / `include/drivers/timer/pit.h`：PIT channel-0 设置。
- `src/kernel/timer/timer.cc` / `include/bigos/timer.h`：从 IRQ 上下文通过
  `on_tick()` 推进的单调 `ticks()` 计数，以及最小 `mdelay` 忙等。`timer_smoke`
  开关输出有界 `BIGOS_TIMER_IRQ` 标记。

### TTY、console 与键盘

键盘输入从 IRQ1 处理器经 scancode 解码进入定容 TTY 输入缓冲；console 输出走 VGA。

- `src/kernel/terminal/keyboard.cc` / `include/bigos/keyboard.h`：US Set 1
  scancode 解码与修饰键跟踪；IRQ 只做 bounded decode 和 ring buffer handoff。
- `src/kernel/terminal/tty.cc` / `include/bigos/tty.h`：TTY 输入入队和
  `terminal::init_tty()`。
- `src/kernel/terminal/console.cc` / `include/bigos/console.h`：基于 VGA backend
  的 console 输出。

### 调度器

协作式单核内核线程调度器。

- `src/kernel/sched/sched.cc` / `include/bigos/sched.h`：TCB、round-robin run
  queue、`create_kernel_thread()`、`yield()`、`thread_exit()` 和 `start()`
  （将 boot 上下文收编为 idle 线程）。
- `src/kernel/sched/switch.s`：x86_64 callee-saved context switch。
- timer IRQ 仅记录 bounded reschedule intent，不在 IRQ 返回前抢占。
  `scheduler_smoke` 开关运行两个 worker 线程，输出
  `BIGOS_SCHED_THREAD_A` / `BIGOS_SCHED_THREAD_B`。

### 系统调用

- `src/kernel/syscall/syscall.cc` / `include/bigos/syscall.h`：`int 0x80`
  dispatcher 和最小寄存器 ABI（number 在 `rax`，参数在 `rdi/rsi/rdx/r10/r8/r9`，
  返回值在 `rax`）。实现 `SYS_DEBUG_WRITE`、`SYS_GET_TICK`、`SYS_WRITE` 和
  `SYS_EXIT`；未知 number 返回 `SYS_ENOSYS`。`syscall_smoke` 开关从 ring0 验证。

### 进程与用户态

仅在 `user_program_smoke` 下编译，不参与普通启动。

- `src/kernel/proc/proc.cc` / `include/bigos/proc.h`：最小 `Process`、用户地址
  空间派生、flat embedded 用户镜像映射，以及 `SYS_WRITE`/`SYS_EXIT` smoke
  （`BIGOS_USER_ENTER` / `BIGOS_USER_EXIT`）。
- `src/kernel/proc/user_mode.cc` / `src/kernel/proc/user_mode.s` /
  `include/bigos/user_mode.h`：GDT/TSS/RSP0 设置和 `iretq` ring3 entry。
- 没有 ELF 用户程序加载器、没有 demand paging；`#PF` handler 对用户态页错误
  只记录受控 marker。

### 显示与 IO

VGA 文本模式和 COM1 串口是当前输出后端。

- `src/drivers/video/vga.cc`：文本缓冲区写入、光标移动、清屏。
- `src/kernel/bigos/io.cc`：端口 IO 封装、`kprintf` 和串口输出。
- `src/kernel/bigos/utils.cc`：整数转字符串等小型辅助函数。

### 内核 C++ 支持

项目提供了少量 freestanding C++ 基础设施。

- `cpp/include/ktl`：内核容器和工具头文件。
- `cpp/ktl`：KTL 实现。
- `cpp/libsupc++`：最小 ABI 和 `new`/`delete` 支持。
- `include`：轻量标准风格头文件，例如 `stdint.h`、`stddef.h`、
  `stdarg.h` 和 `string.h`。

## 开发注意事项

- 保持代码 freestanding-safe。不要依赖托管 libc、异常、RTTI、OS 服务，
  或尚未初始化的动态分配路径。
- 将引导地址、链接地址、页表布局、中断向量和磁盘偏移视为关键设计约束。
- 优先使用小而显式的硬件相关代码。
- 仔细验证初始化顺序；许多子系统依赖内存、分页或描述符表先就绪。
- 修改 Bochs 或磁盘镜像设置时，请记录本地路径假设。

## 许可证

BigOS 仅使用 GNU General Public License v3.0 授权。详见 `LICENSE`。
