# BigOS

语言：[English](README.md) | 简体中文

BigOS 是一个早期阶段的 x86_64 操作系统内核，主要使用 freestanding
C++17、C17 和汇编编写。它已从 boot/kernel 骨架迭代为经过 smoke 验证、单核且以同步为主的
研究内核：当前默认可运行基线仍是 x86_64 Legacy BIOS/MBR/exFAT 路径，同时已有一个
不具备运行时等价能力的 x86_64 UEFI boot backend spike。它的有界用户态闭环包括引导流程、
文本/串口输出、中断/异常/syscall 处理、PIT timer tick、键盘驱动的 TTY 输入路径、
具备 bounded timer semantics 的内核线程调度器、`int 0x80` syscall 入口、进程生命周期、
fd/VFS 服务、有界可写 `/rw` 存储、persistent clean-sync `/rw`、cwd/relative path、
受限 rename、metadata 查询、pipe/dup、默认开启的 PID-1 init、最小用户态 crt0/libc、
`/bin/sh`、bounded ELF64 用户程序加载器、VMA-backed 用户内存校验，以及一套相对完整的
早期内核内存管理。

本仓库是一个研究/玩具操作系统内核项目，不是托管应用或服务。

## 状态

已完成能力压缩为当前有界最小可用系统基线：在 boot 路径、
中断基础设施和早期内存管理之上，具备 timer、输入、调度、syscall、有界 POSIX-like
进程/I/O 子集、读写 VFS 原语、bounded 用户 ELF 加载、常驻 PID-1 init、最小静态用户程序、
有界 `/rw` 运行期与 persistent clean-sync 存储、cwd/relative path、受限 rename、
metadata、pipe/dup 和最小用户态运行时的单核、以同步为主的内核。

已经实现或部分实现：

- x86 引导路径，包括 MBR、exFAT DBR、扩展 DBR 和长模式切换。
- 从 exFAT 磁盘镜像加载 ELF64 内核。
- 高半区内核链接地址 `0xffffffff80000000`。
- VGA 文本模式输出、`kprintf`，以及用于确定性标记的 COM1 串口输出。
- kernel-owned 静态 IDT、生成的汇编 ISR 桩，以及把 CPU exception、i8259 IRQ
  与 `int 0x80` syscall 向量分离的稳定 `InterruptFrame` dispatch ABI。
- `#PF` 处理器：读取 `CR2`，恢复受支持的用户 demand-zero/COW fault，并对不可恢复
  的内核 fault 输出 `BIGOS_PAGE_FAULT` 标记。
- 统一的早期致命诊断（`bigos::kpanic`/`khalt`）：在 COM1 与 VGA 输出稳定的
  `BIGOS_PANIC code=<code> source=<source>` 标记，然后关中断并停机。
- i8259 PIC 驱动，以及 IRQ0 上的 PIT timer 驱动，提供单调 tick 和最小 `mdelay`
  忙等原语。
- 键盘 IRQ1 scancode 解码（US Set 1）接入定容 TTY/console 输入路径；console
  输出走 VGA。
- 单核内核线程调度器：1 页内核栈、x86_64 context switch、round-robin
  `yield()`、scheduler-owned idle 线程、wait queue、timeout sleep、
  preemption-disable guard，以及 bounded IRQ-return timer preemption。
- `int 0x80` syscall 入口、最小寄存器 ABI 和 bounded dispatcher，包含用户 libc
  与 shell 使用的进程、fd/VFS、pipe/dup、身份/时间、信号和 `SYS_EXECVE` 调用。
- 默认关闭的首个 ring3 用户程序 smoke：flat embedded image 通过 TSS/RSP0 与
  `iretq` 进入 ring3，并完成 `SYS_WRITE`/`SYS_EXIT` 闭环。
- 默认关闭的用户 ELF smoke：bounded ELF64 `ET_EXEC` 镜像会被打包为
  `/boot/user/init.elf`，从 exFAT 读取、映射进派生用户地址空间，并进入 ring3。
- 默认开启的常驻 C init 打包为 `/boot/user/init.elf`，通过 `fork` + `execve`
  启动并重启 `/bin/sh`；默认关闭的 `userland_smoke` 会用
  `BIGOS_USERLAND_PASSED` 验证 crt0、libc、fork/exec/wait、pipe、重定向和 malloc。
- 进程生命周期核心：PID/parent-child 状态、wait/exit/reap 语义、process-local
  fd table、bounded exec image replacement，以及 exit 或 user fault 后的 safe teardown。
- 内核 block/filesystem 路径和 VFS 壳层：同步 ATA PIO sector 读取、MBR exFAT
  分区发现、只读 exFAT boot assets、有界可写 `/rw` 文件、persistent clean-sync
  存储、cwd/relative path lookup、受限 rename、metadata 查询、fd-backed
  `open`/`read`/`write`/`close`/`lseek`/`fsync`，以及面向受控 raw image 的 bounded
  file read/write。
- 基于 buddy 的物理页分配器，并使用 early metadata arena 完成 bootstrap。
- Slab/kmalloc 分配器：size class、动态 slab 回收、page-backed 大对象分配、
  可选 debug guard 和验证统计。
- 内核虚拟内存分配器（first-fit、四级页表映射、释放时清除 PTE 并刷新 TLB）、
  内核 direct map，以及 C++ `new`/`delete` 集成。
- 用户地址空间 teardown 和 owned 运行时映射的空 PT/PD/PDPT 回收；高半区内核映射
  保持 borrowed。
- Bounded VMA/user-memory API：stack/heap/image 元数据、VMA-backed syscall buffer
  validation、`brk`、restricted anonymous mapping、demand-zero 用户页物化和 COW
  写时分裂；这不是广泛 file-backed demand paging。
- 显式分配 API：内核虚拟页使用 `alloc_kernel_pages(nr_pages, flags)`，
  物理 buddy 使用内部 `alloc_physical_order(order, flags)`。
- 可切换的早期内存运行时自检（`bigos::mm::self_test`）。
- 小型 KTL 支持库，用于内核容器和辅助工具。

尚未实现或仍处于骨架状态：

- UEFI runtime parity、storage/device backend parity、backend cleanup，以及超出当前
  可运行 x86_64 boot backend spike 的广泛 UEFI 验证。
- SMP、per-CPU run queue、跨 CPU migration、完整 priority/realtime scheduling
  和 POSIX scheduling policy。
- 完整 POSIX 多进程模型：广泛进程策略、超出当前 bounded 子集的 `fork` 语义、
  `exec*` 全族、作业控制和终端进程组。
- file-backed `mmap`、广泛 mapping policy、动态链接、共享库和完整 POSIX libc。
- 完整 POSIX filesystem、journaling、crash recovery、async I/O、超出当前受限子集的
  广泛目录变更语义，以及超出当前受控 ATA PIO + exFAT/VFS 子集的广泛存储设备支持。
- 更广泛的设备驱动支持。
- 完整的构建/安装自动化与 CI。

## 仓库结构

```text
.
|-- cpp               内核 C++ 支持库、KTL、libsupc++ 子集
|-- include           公共内核头文件和小型 libc 风格头文件子集
|-- user              freestanding 用户 crt0/libc、init、shell、bin 与 smoke
|-- kernel            内核实现源码
|   |-- arch/x86/boot x86 引导代码、MBR/DBR 和 ELF 加载器
|   |-- core          内核入口及子系统：irq、timer、terminal（console/
|   |                 keyboard/tty）、sched、syscall、proc、fs、底层 IO
|   |-- drivers       VGA、i8259 PIC、PIT timer、ATA PIO 等硬件驱动
|   |-- mm            buddy、slab、kmalloc、虚拟内存和 direct map 代码
|   `-- runtime       运行时启动汇编源码对象
|-- tools             boot 磁盘安装工具等开发辅助脚本
|-- openspec          OpenSpec 项目配置
|-- tests             验证测试和本地测试资产
|-- link.lds          内核链接脚本
|-- xmake             拆分后的 xmake include 与交叉工具链定义
|-- xmake.lua         主构建入口
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
  - 可选运行 syscall、scheduler、blocking、scheduler-semantics、block/exFAT、首个用户程序和用户 ELF smoke
  - 通过 sched::start() 进入单核调度器；停机行为由调度器 idle 线程拥有，
    取代裸 hlt 尾循环
```

关键文件：

- `kernel/arch/x86/boot/boot.s`：早期 CPU 模式切换并跳转到内核。
- `kernel/arch/x86/boot/boot.cc`：磁盘读取、exFAT 查找、ELF 加载。
- `kernel/core/kernel.cc`：主内核入口。
- `link.lds`：高半区内核布局。
- `docs/zh/arch/x86-boot-layout.md`：当前 Legacy BIOS 地址和 handoff 布局。
- `docs/zh/arch/uefi-boot-blueprint.md`：x86_64 UEFI boot backend spike 说明，
  以及未来 runtime parity/backend cleanup 边界。

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
xmake f --scheduler_semantics_smoke=y # BIGOS_SCHEDULER_SEMANTICS_SMOKE -> BIGOS_SCHED_SEMANTICS_PASSED
xmake f --blocking_smoke=y    # BIGOS_BLOCKING_SMOKE -> BIGOS_BLOCKING_SMOKE_PASSED
xmake f --user_vmem_smoke=y   # BIGOS_USER_VMEM_SMOKE -> BIGOS_USER_VMEM_SMOKE_PASSED/FAILED
xmake f --syscall_smoke=y     # BIGOS_SYSCALL_SMOKE -> BIGOS_SYSCALL_SMOKE_PASSED/FAILED
xmake f --user_program_smoke=y # BIGOS_USER_PROGRAM_SMOKE -> BIGOS_USER_ENTER/EXIT
xmake f --fs_smoke=y          # BIGOS_FS_SMOKE -> BIGOS_FS_EXFAT_READ_PASSED/FAILED
xmake f --block_io_request_smoke=y # BIGOS_BLOCK_IO_REQUEST_SMOKE -> BIGOS_BLOCK_IO_REQUEST_PASSED/FAILED
xmake f --user_elf_smoke=y    # BIGOS_USER_ELF_SMOKE -> BIGOS_USER_ENTER/EXIT
xmake f --demand_paging_smoke=y # BIGOS_DEMAND_PAGING_SMOKE -> BIGOS_DEMAND_PAGING_PASSED/FAILED
xmake f --fork_cow_smoke=y    # BIGOS_FORK_COW_SMOKE -> BIGOS_FORK_COW_PASSED/FAILED
xmake f --time_identity_smoke=y # BIGOS_TIME_IDENTITY_SMOKE -> BIGOS_TIME_IDENTITY_PASSED/FAILED
xmake f --signal_smoke=y      # BIGOS_SIGNAL_SMOKE -> BIGOS_SIGNAL_PASSED/FAILED
xmake f --writable_fs_smoke=y # BIGOS_WRITABLE_FS_SMOKE -> BIGOS_WRITABLE_FS_PASSED/FAILED
xmake f --pipe_smoke=y        # BIGOS_PIPE_SMOKE -> BIGOS_PIPE_PASSED/FAILED
xmake f --userland_smoke=y    # BIGOS_USERLAND_SMOKE -> BIGOS_USERLAND_PASSED/FAILED
```

`--mm_self_test` 会自动启用 `--slab_debug`。自检会在 COM1 与 VGA 输出
`BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` 标记。
普通启动会打包 `/boot/user/init.elf`、`/bin/sh` 和 `/bin/*` 辅助程序，进入
PID-1 init，并启动 `/bin/sh`；默认 headless 验证观察 `BIGOS_USER_EXEC`。
`--user_program_smoke`、`--user_elf_smoke` 和 `--userland_smoke` 会选择默认关闭的
用户程序验证路径，替代普通用户 init payload。所有标记都写到 COM1 串口和 VGA。

当前 Legacy BIOS/MBR/exFAT 路径的本地 emulator 运行入口：

```bash
xmake run qemu
xmake run qemu -- --display none
xmake run qemu-gdb
xmake run bochs
xmake run bochs -- --display sdl2
xmake run bochs -- --display none
```

`xmake run qemu` 使用图形 QEMU 启动生成的 raw image，并把 COM1 输出写入
`build/test/qemu.serial.log`。`xmake run qemu -- --display none` 以无交互显示模式
启动 QEMU。`xmake run qemu-gdb` 会让 QEMU 在启动前暂停并开启默认 GDB stub
（`target remote localhost:1234`），COM1 输出写入 `build/test/qemu-gdb.serial.log`。
`xmake run bochs` 默认启动 SDL2 Bochs 流程；`xmake run bochs -- --display sdl2`
显式选择 SDL2，`xmake run bochs -- --display none` 选择 Bochs no-GUI 模式。这些
target 都通过 xmake 构建 kernel 和 boot artifacts，然后以 `--skip-build` 调用
Python 镜像辅助脚本，并转发 `--` 后的 target arguments。

Python helper 仍用于 raw image 生成、Bochs 配置生成、QEMU 启动命令查看、
serial marker 检查和 no-launch/offline validation：

```bash
uv run python -m tools.bigosdev run
```

helper 会执行 preflight 检查；如果不是从 xmake run target 调用，也可以构建 kernel
和 boot artifacts。随后它在用户态生成 raw 磁盘镜像，写入 MBR、exFAT boot
region、`/boot/boot.bin`、根目录 `kernel`、`/boot/fs_smoke.txt`、
`/boot/user/init.elf` 和打包的 `/bin/*` 用户程序，并在未指定 `--no-launch`
时启动选定 emulator。它不会构建 UEFI loader、ESP 镜像、OVMF 配置或新存储驱动路径。

默认生成物均位于 `build/` 下：

- raw 磁盘镜像：`build/test/os.raw`。
- 生成的 Bochs 配置：`build/test/bochsrc.bxrc`。
- QEMU 串口日志：`build/test/qemu.serial.log` 和
  `build/test/qemu-gdb.serial.log`。
- boot 产物：`build/bin/x86/boot/`。
- 内核 ELF：`build/kernel`。

常用参数示例：

```bash
uv run python -m tools.bigosdev run --image build/test/debug.raw --image-size 128M
uv run python -m tools.bigosdev image create
uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED
uv run python -m tools.bigosdev run --emulator qemu-gdb
uv run python -m tools.bigosdev run --romimage /path/to/BIOS-bochs-latest --vgaromimage /path/to/VGABIOS-lgpl-latest
uv run python -m tools.bigosdev run --serial-log logs/serial.log --expect-serial-marker BIGOS_MM_SELF_TEST_PASSED
uv run python -m tools.bigosdev image validate --image build/test/os.raw
```

运行 xmake target 前，使用 `xmake f ...=y` 配置 smoke 开关。Python helper 不是
权威的 smoke 开关配置入口。

raw image 由 Python 标准库直接写入生成，不依赖 macOS `diskutil`、Linux loop
device、挂载权限、`mkfs.exfat` 或手工准备的 exFAT 镜像。

当前范围：

- 该流程支持 QEMU 与 Bochs。
- QEMU 使用 Legacy BIOS 默认路径，并用 `-drive file=<image>,format=raw,if=ide`
  把 raw image 暴露为 IDE disk，保持当前 ATA PIO 路径。
- QEMU headless automation 通过 helper 的 display 参数实现，例如
  `--emulator qemu --display none`；不新增独立 `qemu-headless` xmake target。
- `xmake run bochs` 是默认 SDL2 的 Legacy BIOS 调试入口；使用 `xmake run
  bochs -- --display sdl2` 可显式选择 SDL2，使用 `xmake run bochs -- --display
  none` 可选择 Bochs no-GUI 模式。Bochs 仍适合早期 boot、BIOS、ATA PIO、中断和硬件行为差异排查。
- UEFI 仍是独立的非等价 backend spike 路径；storage/device parity 和更广泛的
  runtime validation 仍是未来 backend 工作。
- 该流程不修改 `boot.s`、`boot.cc`、`BootInfo`、`link.lds`、高半区内核地址或
  内核运行时初始化顺序。

常见失败原因：

- 缺少 `xmake`、选定 emulator（`qemu-system-x86_64` 或 `bochs`）、`python3`
  或 `x86_64-elf-*` 工具时，会在 preflight 阶段按需失败，且不会生成或覆盖镜像。
- 内核或 boot 构建失败时会停止流程，不会继续使用 stale 产物启动。
- 如果 Bochs 安装需要宿主机特定 BIOS/VGA BIOS 路径，可以使用 `--romimage`、
  `--vgaromimage`、`--bochsrc` 或 `--bochs-extra` 指定。
- 历史 `test/bochsrc.bxrc` 只能作为宿主机配置参考；生成的
  `build/test/bochsrc.bxrc` 默认不会硬编码 Windows 路径、`win32` display
  设置或固定 ROM 路径。

运行生成的 Bochs 流程：

```bash
xmake run qemu
xmake run qemu -- --display none
xmake run qemu-gdb
xmake run bochs
xmake run bochs -- --display sdl2
xmake run bochs -- --display none
```

注意事项：

- 构建前请安装或暴露 `x86_64-elf-gcc`、`x86_64-elf-g++`、
  `x86_64-elf-ld` 以及相关 binutils。
- 使用 `xmake run qemu` 或 `xmake run qemu-gdb` 前请安装 QEMU；使用 Bochs
  target 前请安装 Bochs。
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
默认可运行 backend 是 Legacy BIOS。当前已有可运行的 x86_64 UEFI boot backend
spike，但它不是 Legacy BIOS/MBR/exFAT 路径的 runtime-parity 替代。

- `kernel/arch/x86/boot/mbr.s`：第一阶段引导代码。
- `kernel/arch/x86/boot/dbr_exfat.s`：exFAT 引导扇区代码。
- `kernel/arch/x86/boot/exdbr_exfat.s`：扩展 exFAT 引导代码。
- `kernel/arch/x86/boot/boot.s`：模式切换、早期页表、转入长模式。
- `kernel/arch/x86/boot/boot.cc`：ATA 磁盘读取、exFAT 目录扫描、ELF64 加载。
- `tools.bigosdev image patch`：用于将引导扇区写入虚拟磁盘镜像的辅助工具。

### 内核入口

`kernel/core/kernel.cc` 当前执行的运行时设置：

- 清空 VGA 文本屏幕并初始化 COM1 串口输出。
- 调用 `bigos::init_mem()`。
- 可选运行 `bigos::mm::self_test()`（以 `BIGOS_MM_SELF_TEST` 构建时）和用户
  地址空间 vmem smoke（`BIGOS_USER_VMEM_SMOKE`）。
- 初始化 TTY/console 输入路径（`bigos::terminal::init_tty()`）。
- 调用 `bigos::irq::initIRQ()`（IDT、异常/IRQ/syscall 分发、PIC、IRQ0 上的 PIT
  timer、keyboard IRQ1）。
- 可选触发 page-fault smoke（以 `BIGOS_PAGE_FAULT_SMOKE` 构建时）。
- 开启中断并输出 "BigOS kernel reached" 标记。
- 可选运行 syscall、scheduler、blocking、scheduler-semantics、block/exFAT、首个用户程序和用户 ELF smoke。
- 通过 `bigos::sched::start()` 进入单核调度器；停机行为由 idle 线程拥有。

### 内存管理

内存子系统位于 `kernel/mm/`，是内核中最完善的部分。公开 API 通过命名区分分配层级：
内核虚拟页使用页数语义 `alloc_kernel_pages(nr_pages, flags)`，内部物理分配使用
buddy order 语义 `alloc_physical_order(order, flags)`，旧的混合语义 alias 已移除。

- `kernel/mm/buddy.cc` / `kernel/mm/buddy.h`：解析 BIOS 内存映射，划分 DMA/DMA32/NORMAL
  区域，管理物理页块，并使用 early metadata arena 完成 bootstrap，使初始化不依赖
  动态 slab 扩容。
- `kernel/mm/slab.cc` / `kernel/mm/slab.h`：size class 缓存、`kmalloc/free`、动态 slab
  回收、page-backed 大对象分配、可选 debug guard 和统计。
- `kernel/mm/kmem.cc` / `kernel/mm/kmem.h`：kmalloc/free 集成与 cache 接线。
- `kernel/mm/vmem.cc` / `kernel/mm/vmem.h`：first-fit 内核虚拟区间、四级页表映射、
  释放时清除 PTE 并刷新 TLB。
- `kernel/mm/memory.cc`：公开内存 API 入口。
- `kernel/mm/self_test.cc`：可切换的早期运行时自检，输出确定性
  `BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` 标记。
- `include/bigos/memory.h`：暴露公共内存分配 API。
- `kernel/mm/memdef.h`：定义 mm 私有的页大小、buddy 阶数和分配标志。

自检用法见 `docs/zh/arch/memory-runtime-validation.md`。

### 中断与输入

中断子系统结合了汇编桩和 C++ 描述符。内核在开启可屏蔽中断前加载 kernel-owned
静态 IDT，并通过稳定的 `InterruptFrame` dispatch ABI 路由所有 ISR 入口。

- `kernel/core/irq/interrupt.s`：生成的 ISR 入口桩。
- `kernel/core/irq/interrupt.cc`：IDT 初始化、异常分发、外部 IRQ 分发，以及
  `int 0x80` syscall 向量路由。
- `kernel/drivers/irqchip/i8259.cc`：PIC remap、屏蔽和 EOI 支持。
- `kernel/core/irq/isr.cc`：诊断型 `#PF` 处理器、timer IRQ0 tick 钩子和
  keyboard IRQ1 处理器。
- `include/irq/interrupt.h`：描述符布局、`InterruptFrame` 和向量常量。
- `docs/zh/arch/interrupt-exception-foundation.md`：当前中断/异常设计、非目标和验证记录。

CPU exception（`0x00`-`0x1f`）、remap 后的 i8259 IRQ（`0x20`-`0x2f`）和 syscall
向量（`0x80`）分开分发，只有外部 IRQ 才发送 EOI。syscall IDT gate 提升到 DPL=3
以便 ring3 触发 `int 0x80`，exception 和外部 IRQ gate 仍保持 ring0-only。

### Timer

PIT 在 IRQ0 上驱动周期性 tick。

- `kernel/drivers/timer/pit.cc` / `include/drivers/timer/pit.h`：PIT channel-0 设置。
- `kernel/core/timer/timer.cc` / `include/bigos/timer.h`：从 IRQ 上下文通过
  `on_tick()` 推进的单调 `ticks()` 计数，以及最小 `mdelay` 忙等。`timer_smoke`
  开关输出有界 `BIGOS_TIMER_IRQ` 标记。

### TTY、console 与键盘

键盘输入从 IRQ1 处理器经 scancode 解码进入定容 TTY 输入缓冲；console 输出走 VGA。

- `kernel/core/terminal/keyboard.cc` / `include/bigos/keyboard.h`：US Set 1
  scancode 解码与修饰键跟踪；IRQ 只做 bounded decode 和 ring buffer handoff。
- `kernel/core/terminal/tty.cc` / `include/bigos/tty.h`：TTY 输入入队和
  `terminal::init_tty()`。
- `kernel/core/terminal/console.cc` / `include/bigos/console.h`：基于 VGA backend
  的 console 输出。

### 调度器

单核 round-robin 内核线程调度器，包含 cooperative switch 路径、wait queue、
timeout sleep 和受 guard 保护的 IRQ-return timer preemption。

- `kernel/core/sched/sched.cc` / `include/bigos/sched.h`：TCB、round-robin run
  queue、intrusive wait/sleep list、`create_kernel_thread()`、`yield()`、
  `thread_exit()`、`sleep_for()`、wakeup API、preemption guard 和 `start()`
  （将 boot 上下文收编为 idle 线程）。
- `kernel/core/sched/switch.s`：x86_64 callee-saved context switch。
- timer IRQ 统计普通线程 time slice、唤醒到期 sleeper，并记录 bounded reschedule
  intent。外部 IRQ return 只有在 handler 完成、单次 EOI 已发送且 scheduler guard
  允许后才可能切换。
- `scheduler_smoke`、`blocking_smoke` 和 `scheduler_semantics_smoke` 分别验证
  cooperative switching、wait/timeout 行为和 IRQ-return preemption marker。

### 系统调用

- `kernel/core/syscall/syscall.cc` / `include/bigos/syscall.h`：`int 0x80`
  dispatcher 和最小寄存器 ABI（number 在 `rax`，参数在 `rdi/rsi/rdx/r10/r8/r9`，
  返回值在 `rax`）。实现 `SYS_DEBUG_WRITE`、`SYS_GET_TICK`、`SYS_WRITE`、
  `SYS_EXIT`、`SYS_WAIT`、fd/VFS `SYS_OPEN`/`SYS_READ`/`SYS_CLOSE`、`SYS_BRK`、
  restricted `SYS_MAP_ANON`、`SYS_FORK`、时间/身份、signal、`lseek`、pipe/dup、
  fsync/mkdir/unlink 和 `SYS_EXECVE`；未知 number 返回 `SYS_ENOSYS`。
  `syscall_smoke` 开关从 ring0 验证诊断 dispatch。

### 进程与用户态

生命周期核心作为普通内核子系统编译。普通启动进入常驻 PID-1 init 和 `/bin/sh`；
`user_program_smoke`、`user_elf_smoke` 和 `userland_smoke` 只选择默认关闭的验证 payload。

- `kernel/core/proc/proc.cc` / `include/bigos/proc.h`：bounded process model、PID
  分配、parent/child 链接、wait status、zombie/reap-pending lifecycle、
  process-local fd table、VMA/heap metadata、VMA-backed user-buffer validation、
  `brk`、restricted anonymous mapping、demand-zero 物化、bounded `fork`/COW、
  signal 状态、用户地址空间派生、安全 teardown/reaping、flat embedded 用户镜像映射，
  以及面向 `/boot/user/init.elf`、`execve`、PID-1 init 和默认关闭用户态 smoke 的
  bounded ELF64 `ET_EXEC` prepare/exec 路径。
- `kernel/core/proc/user_mode.cc` / `kernel/core/proc/user_mode.s` /
  `include/bigos/user_mode.h`：GDT/TSS/RSP0 设置和 `iretq` ring3 entry。
- Demand paging 与 COW 仅针对当前 bounded anonymous 用户映射实现；广泛
  file-backed `mmap`、动态链接、作业控制和完整 POSIX libc 仍不在范围内。

### 显示与 IO

VGA 文本模式和 COM1 串口是当前输出后端。

- `kernel/drivers/video/vga.cc`：文本缓冲区写入、光标移动、清屏。
- `kernel/core/bigos/io.cc`：端口 IO 封装、`kprintf` 和串口输出。
- `kernel/core/bigos/utils.cc`：整数转字符串等小型辅助函数。

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
- 描述 fd/VFS、process lifecycle 和 VMA/user-memory 时保持 bounded 边界：同步 I/O、
  RAM-backed 与 persistent clean-sync `/rw`、受限 rename/metadata/cwd-relative path、
  restricted anonymous/file mapping、bounded demand paging/COW、无广泛 POSIX 进程模型、
  无动态链接、无作业控制、无 journaling/crash recovery、无广泛 file-backed `mmap`。
- 优先使用小而显式的硬件相关代码。
- 仔细验证初始化顺序；许多子系统依赖内存、分页或描述符表先就绪。
- 修改 Bochs 或磁盘镜像设置时，请记录本地路径假设。

## 许可证

BigOS 仅使用 GNU General Public License v3.0 授权。详见 `LICENSE`。
