# x86 Legacy 启动布局

BigOS 当前使用 legacy BIOS 路径：

```text
BIOS -> MBR -> exFAT DBR -> extended DBR -> boot.bin -> ELF64 kernel
```

该路径仍是显式可运行的兼容后端，也是 Legacy BIOS kernel handoff 数据的生产者。
`docs/zh/arch/uefi-boot-blueprint.md` 中的 x86_64 UEFI backend 现在是增量用户态基线内的
默认可运行后端；它不会替换 MBR/DBR/exDBR/`boot.bin` 流程。当前 Legacy 调试入口是
`xmake run qemu-legacy`、`xmake run qemu-gdb` 和带 `--display sdl2|none` 的 `xmake run bochs`。

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
0xffff800000000000..0xffff80ffffffffff  recursive self-mapping window
0xffff880000000000..0xffff88ffffffffff  KVMEM heap/vmalloc-style 分配窗口，不是 direct map
0xffff900000000000..0xffffcfffffffffff  kernel direct map，仅映射 ordinary RAM
0xffffffff80000000  内核 higher-half 虚拟基址
```

`BootInfo` v1 定义在 `include/arch/x86/boot/boot_info.h`。Boot C++ 会继续将其
写入 `BIGOS_BOOT_INFO_ADDRESS` (`0x0840`)，并在内核消费者迁移期间保留 legacy
别名。它的 magic、version、size、字段偏移、对齐方式和固定地址仍是兼容性 ABI。

## Kernel ELF segment 布局

`link.lds` 将 higher-half kernel 保持在 `0xffffffff80000000`，入口仍为
`_start`，并将 loadable ELF program headers 拆成三个权限类别：

- `text`：`PT_LOAD FLAGS(5)`，即 `PF_R | PF_X`。它覆盖 `.bigos`、`.init`、`.text`
  和 `.fini`，因此 `_start` 位于 RX segment 内。
- `rodata`：`PT_LOAD FLAGS(4)`，即 `PF_R`。它覆盖 `.rodata`、`.rodata1`、只读
  `.eh_frame_hdr` 和只读 `.eh_frame`。
- `data`：`PT_LOAD FLAGS(6)`，即 `PF_R | PF_W`。它覆盖 `.ctors`、`.dtors`、
  `.data`、`.4k_area` 和 `.bss`，其中 `.4k_area` 同时收集历史 `.4k_area` 名称和
  当前 `_section_4k_align_` 使用的 `.4k_align_area` input section。

`text` 到 `rodata`、`rodata` 到 `data` 的权限类别边界使用 4KiB 对齐，避免未来按页
收敛权限时让可执行内容和可写内容共享同一页。当前 change 只修正 ELF program
header 权限布局，不启用运行时页级 W^X；Legacy BIOS bootloader 仍建立可写的
higher-half 页表映射，并继续通过 BootInfo 传递固定的 kernel load base、entry 和
memory extent。

`.ctors` 和 `.dtors` 本 change 保持在 RW `data` segment，避免在修正 ELF layout 时
同时改变 legacy C++ runtime 构造/析构表的可写性假设。后续若要启用 kernel
text/rodata/data 页级权限收敛，优先由 `link.lds` 暴露页对齐的 linker 边界符号，再
由 kernel virtual memory 初始化消费这些边界；直接消费 ELF `p_flags` 需要另起
BootInfo/loader segment metadata 设计。

布局验证优先使用：

- `xmake -r`：确认交叉构建成功且不再出现 `LOAD segment with RWX permissions`。
- `x86_64-elf-objdump -p build/kernel` 或 `x86_64-elf-readelf -l build/kernel`：确认
  三个 `LOAD` segment 分别为 `r-x`、`r--`、`rw-`，且没有 `rwx`。
- `x86_64-elf-objdump -f build/kernel`：确认 entry point 仍为
  `0xffffffff80000000`。
- `x86_64-elf-objdump -h build/kernel`：确认 `.bigos/.init/.text/.fini`、只读
  `.rodata/.eh_frame*`、`.ctors/.dtors/.data/.4k_area/.bss` 分别落入预期地址范围。

主要 handoff 路径现在是 `BootInfo` v2。Legacy BIOS boot C++ 会在
`0x9000..0x9fff` 构建一个有界的 `BootInfoHeader + BootInfoSection[]` blob，
随后 `boot.s` 在跳转到内核 ELF 入口之前，通过 `rdi` 传递它的
`BootInfoHeader*`。v2 blob 地址是该后端生产者侧的实现细节；内核 ABI 是通过
寄存器传入的指针，以及相对于 header 基址的 section 偏移。

v2 blob 当前包含两个必需 section：

- `core`：Legacy BIOS 协议元数据、启动驱动器、exFAT 数据区 LBA、内核加载虚拟地址、内核入口虚拟地址、内核文件大小和内核内存大小。
- `memory_map`：从 BIOS E820 ARDS 规范化得到的 `BootMemoryRegion[]` 条目。

UEFI backend 也会生产 `BootInfo` v2 blob，但其 blob 地址由 loader 分配，而不是 Legacy 固定
`0x9000..0x9fff` 区域。它的 required `core` section 标识 UEFI boot protocol，并将
`exfat_data_area_lba` 写为零，因此不会把 ESP/root storage provenance 复用到 Legacy
exFAT 字段。UEFI storage provenance 与 loader diagnostics 位于 optional
`storage_metadata` 和 `loader_metadata` sections；kernel 启动校验仍只依赖有效的 required
`core` 与 `memory_map` sections，未知 optional sections 仍可跳过。

v2 magic 独立于 v1 magic，因此消费者不会只依赖 `version` 来区分固定的 v1
结构体与 header/section blob。Section table 与 payload 偏移都相对于
`BootInfoHeader`，消费者会检查 header 大小、总大小、section table 边界、
payload 边界、必需 section 是否存在，以及 payload 对齐。未知的可选 section
会被跳过；缺失或格式错误的必需 section 会使 v2 被拒绝，并允许显式回退到 v1
固定地址。

位于 `0x9000..0x9fff` 的 v2 blob 不会移动或重叠 E820 缓冲区、legacy 元数据别名、
v1 `BootInfo`、启动阶段页表、内核 higher-half 页表后备区域、内核物理加载基址或
higher-half 虚拟基址。未来如果新增固定低地址、页表保留区或 handoff 别名，必须
更新该布局，并说明它们与默认 UEFI backend 及后续 parity 工作的兼容性。

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

UEFI backend 将 `EfiConventionalMemory` 映射为 `usable`，保留 UEFI source type/value/attributes，
将 runtime descriptor 映射为 `runtime`，将 MMIO descriptor 映射为 `mmio`，将 ACPI
descriptor 映射为 `acpi_reclaim` 或 `acpi_nvs`，并保守地让 loader-owned、boot-services-owned、
bad、unknown 或 reserved descriptor 不进入初始 free page pool。

内核 direct map 在 `init_buddy()` 和 `init_vmem()` 之后、`BIGOS_MM_SELF_TEST` 之前建立。它使用
`KDIRECT_BASE = 0xffff900000000000`、`KDIRECT_LEN = 0x400000000000`，只从
BootInfo memory map 中选择 page-aligned ordinary RAM 范围，并映射为
`direct = KDIRECT_BASE + physical`。`KVMEM_BASE` 继续表示 kernel heap/vmalloc-style
虚拟分配窗口，不承诺与物理地址存在线性关系；recursive self-mapping window、low
identity map、higher-half kernel base 和固定 boot handoff 地址也不会被 direct map
重定义。

Direct map 首版不覆盖 MMIO、framebuffer、ACPI reclaim/NVS、firmware reserved、
runtime、bad memory 或未知类型。后续设备 BAR、APIC、framebuffer 或带缓存属性要求的
device memory 需要独立 MMIO mapping API，而不是复用 ordinary-RAM direct map helper。

保护模式下的 extended DBR 阶段会使用 ATA primary-master PIO 读取 `boot.bin`。
因此它要求 BIOS 启动驱动器为 `0x80`；其他 BIOS 驱动器编号会使系统暂停，并在
VGA 文本内存中显示可见的 `U` 代码。

`xmake run qemu-legacy` 和 `xmake run qemu-gdb` 通过 QEMU 的 IDE disk 路径
（`-drive file=<image>,format=raw,if=ide`）使用同一个 Legacy BIOS/MBR/exFAT raw image。
`xmake run bochs` 保持现有 Bochs 调试入口，并通过 `--display sdl2|none` target arguments 选择 SDL2 或 no-GUI display。
这些入口不要求 `BOOTX64.EFI`、ESP/FAT 镜像、QEMU/OVMF、Secure Boot、GOP framebuffer、ACPI handoff、Runtime Services、virtio、AHCI/SATA、NVMe 或新存储驱动。

默认 UEFI 调试路径是 `xmake run qemu` 或
`uv run python -m tools.bigosdev run --boot-mode uefi --emulator qemu` 会构建/使用
`build/bin/x86/uefi/BOOTX64.EFI`，生成 `build/test/uefi-esp.img`，准备 `build/test/uefi-root.raw`
作为当前 exFAT runtime root 兼容镜像，复制 OVMF vars template 到
`build/test/OVMF_VARS.uefi.fd`，并将 UEFI 串口输出写入 `logs/qemu-uefi.serial.log`。
显式传入的串口日志路径也必须位于 `logs/` 下；位于 `logs/` 之外的路径会被拒绝。
`xmake run qemu-uefi` 仍是显式别名。该路径使用 QEMU/OVMF 和 ESP/FAT image，并与 Bochs config、BIOS boot sectors
和 `boot.bin` 保持隔离。
