# BigOS

语言：[English](README.md) | 简体中文

BigOS 是一个早期阶段的 x86_64 操作系统内核，主要使用 freestanding
C++17、C17 和汇编编写。目前项目重点在于引导流程、基础文本输出、中断设置、
键盘输入处理，以及早期内核内存管理。

本仓库是一个研究/玩具操作系统内核项目，不是托管应用或服务。

## 状态

项目目前处于内核基础设施引导阶段。

已经实现或部分实现：

- x86 引导路径，包括 MBR、exFAT DBR、扩展 DBR 和长模式切换。
- 从 exFAT 磁盘镜像加载 ELF64 内核。
- 高半区内核链接地址 `0xffffffff80000000`。
- VGA 文本模式输出和简单的 `kprintf` 支持。
- IDT 设置和汇编中断桩。
- i8259 PIC 驱动和键盘扫描码解析。
- 基于 buddy 的物理页分配。
- Slab/kmalloc 分配器，以及 C++ `new`/`delete` 集成。
- 早期虚拟内存分配和页表映射框架。
- 小型 KTL 支持库，用于内核容器和辅助工具。

尚未实现或仍处于骨架状态：

- 基础 VGA 输出之外的 TTY 和控制台抽象。
- 调度器、线程、进程和用户态。
- 系统调用。
- 内核内的文件系统服务。
- 更广泛的设备驱动支持。
- 完整的构建/安装自动化。

## 仓库结构

```text
.
|-- arch/x86/boot     x86 引导代码、MBR/DBR、ELF 加载器、磁盘安装辅助工具
|-- cpp               内核 C++ 支持库、KTL、libsupc++ 子集
|-- drivers           VGA、i8259 PIC 等硬件驱动
|-- include           公共内核头文件和小型 libc 风格头文件子集
|-- kernel            内核入口、IRQ、IO、字符串、控制台/TTY 骨架
|-- lib/src           运行时启动汇编对象
|-- mm                buddy、slab、kmalloc 和虚拟内存代码
|-- openspec          OpenSpec 项目配置
|-- test              Bochs 配置、虚拟磁盘镜像、底层测试片段
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
  - 初始化内存管理
  - 初始化 IRQ 描述符
  - 开启中断
  - 进入 idle hlt 循环
```

关键文件：

- `arch/x86/boot/boot.s`：早期 CPU 模式切换并跳转到内核。
- `arch/x86/boot/boot.cc`：磁盘读取、exFAT 查找、ELF 加载。
- `kernel/kernel.cc`：主内核入口。
- `link.lds`：高半区内核布局。

## 构建与运行

主要构建系统是 xmake，预期编译器是 `x86_64-elf-gcc`。

```bash
xmake
```

在 Bochs 中运行：

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

- `mbr.s`：第一阶段引导代码。
- `dbr_exfat.s`：exFAT 引导扇区代码。
- `exdbr_exfat.s`：扩展 exFAT 引导代码。
- `boot.s`：模式切换、早期页表、转入长模式。
- `boot.cc`：ATA 磁盘读取、exFAT 目录扫描、ELF64 加载。
- `install.py`：用于将引导扇区写入虚拟磁盘镜像的辅助工具。

### 内核入口

`kernel/kernel.cc` 当前执行最小运行时设置：

- 清空 VGA 文本屏幕。
- 调用 `bigos::init_mem()`。
- 调用 `bigos::irq::initIRQ()`。
- 开启中断。
- 使用 `hlt` 空闲等待。

### 内存管理

内存子系统位于 `mm/`。

- `buddy.cc`：解析 BIOS 内存映射，划分 DMA/DMA32/NORMAL 区域，并管理物理页块。
- `slab.cc` 和 `kmem.cc`：提供固定大小缓存和 `kmalloc/free`。
- `vmem.cc`：跟踪内核虚拟地址块，并可预映射已分配页面。
- `memdef.h`：定义页大小、buddy 阶数和分配标志。

### 中断与输入

中断子系统结合了汇编桩和 C++ 描述符。

- `kernel/irq/interrupt.s`：生成的 ISR 入口桩。
- `kernel/irq/interrupt.cc`：IDT 初始化和默认 IRQ 处理器。
- `drivers/irqchip/i8259.cc`：PIC 屏蔽和 EOI 支持。
- `kernel/irq/isr/isr_keyboard.cc`：键盘扫描码解析和临时 VGA 字符输出。

键盘输入尚未接入完整 TTY 层。

### 显示与 IO

VGA 文本模式是当前显示后端。

- `drivers/video/vga.cc`：文本缓冲区写入、光标移动、清屏。
- `kernel/bigos/io.cc`：端口 IO 封装和基础内核打印。

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
