# x86 Legacy 启动布局

BigOS 当前使用 legacy BIOS 路径：

```text
BIOS -> MBR -> exFAT DBR -> extended DBR -> boot.bin -> ELF64 kernel
```

该路径仍是当前可运行的启动后端，也是现有内核所用 kernel handoff 数据的生产者。
`docs/arch/uefi-boot-blueprint.md` 中的 UEFI 计划会把该路径视为未来统一
handoff 模型中的 Legacy 后端；它不会替换 MBR/DBR/exDBR/`boot.bin` 流程，
也不会改变 `make boot-debug` 的含义。

早期启动路径依赖以下固定物理地址和虚拟地址：

```text
0x0500..0x07ff  extended DBR 写入的 E820 ARDS 记录
0x0800..0x083f  legacy 启动元数据别名
0x0800          legacy E820 条目数量
0x0802          BIOS 启动驱动器
0x080c          legacy 内核内存大小
0x0830          legacy exFAT 数据区 LBA
0x0840..0x0887  规范 BootInfo handoff 结构
0x1000..0x1fff  extended DBR 加载区域
0x2000..0x6fff  启动阶段 PML4/PDPT/PD/PT 设置区域
0x5000..        内核 higher-half 页目录 handoff 区域
0x7c00          BIOS 加载的 MBR/DBR 扇区
0x9000..0x9fff  Legacy BIOS 生成的 BootInfo v2 handoff blob
0x0f000         exFAT 目录缓冲区
0x10000         boot.bin 加载地址
0x100000        内核 higher-half 页表后备区域
0x1000000       内核物理加载基址
0xffffffff80000000  内核 higher-half 虚拟基址
```

`BootInfo` v1 定义在 `include/arch/x86/boot/boot_info.h`。Boot C++ 会继续将其
写入 `BIGOS_BOOT_INFO_ADDRESS` (`0x0840`)，并在内核消费者迁移期间保留 legacy
别名。它的 magic、version、size、字段偏移、对齐方式和固定地址仍是兼容性 ABI。

主要 handoff 路径现在是 `BootInfo` v2。Legacy BIOS boot C++ 会在
`0x9000..0x9fff` 构建一个有界的 `BootInfoHeader + BootInfoSection[]` blob，
随后 `boot.s` 在跳转到内核 ELF 入口之前，通过 `rdi` 传递它的
`BootInfoHeader*`。v2 blob 地址是该后端生产者侧的实现细节；内核 ABI 是通过
寄存器传入的指针，以及相对于 header 基址的 section 偏移。

v2 blob 当前包含两个必需 section：

- `core`：Legacy BIOS 协议元数据、启动驱动器、exFAT 数据区 LBA、内核加载虚拟地址、内核入口虚拟地址、内核文件大小和内核内存大小。
- `memory_map`：从 BIOS E820 ARDS 规范化得到的 `BootMemoryRegion[]` 条目。

v2 magic 独立于 v1 magic，因此消费者不会只依赖 `version` 来区分固定的 v1
结构体与 header/section blob。Section table 与 payload 偏移都相对于
`BootInfoHeader`，消费者会检查 header 大小、总大小、section table 边界、
payload 边界、必需 section 是否存在，以及 payload 对齐。未知的可选 section
会被跳过；缺失或格式错误的必需 section 会使 v2 被拒绝，并允许显式回退到 v1
固定地址。

位于 `0x9000..0x9fff` 的 v2 blob 不会移动或重叠 E820 缓冲区、legacy 元数据别名、
v1 `BootInfo`、启动阶段页表、内核 higher-half 页表后备区域、内核物理加载基址或
higher-half 虚拟基址。未来如果新增固定低地址、页表保留区或 handoff 别名，必须
更新该布局，并说明它们与未来 UEFI 后端的兼容性。

`BootMemoryRegion` 按如下方式映射 BIOS E820：

- E820 类型 `1` 映射为 `usable`；只有这些区域会进入 buddy 空闲链表。
- E820 类型 `2` 映射为 `reserved`。
- E820 类型 `3` 映射为 `acpi_reclaim`。
- E820 类型 `4` 映射为 `acpi_nvs`。
- E820 类型 `5` 映射为 `bad_memory`。
- 未知 E820 类型会保守地映射为 `reserved`。

Reserved、runtime、MMIO、ACPI reclaim、ACPI NVS、bad memory 和未知内存类型在
早期 buddy 初始化期间都不会被释放。`acpi_reclaim` 会保持 reserved，直到未来的
ACPI 表生命周期阶段能够证明其可安全回收。

保护模式下的 extended DBR 阶段会使用 ATA primary-master PIO 读取 `boot.bin`。
因此它要求 BIOS 启动驱动器为 `0x80`；其他 BIOS 驱动器编号会使系统暂停，并在
VGA 文本内存中显示可见的 `U` 代码。

`make boot-debug` 保持现有的 Legacy BIOS/MBR/exFAT/Bochs 含义。该 handoff 变更
不会把该目标切换为 `BOOTX64.EFI`、ESP/FAT 镜像或 QEMU/OVMF。
