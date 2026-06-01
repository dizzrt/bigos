## Why

UEFI 启动蓝图已经明确长期方向是统一 kernel handoff，但当前运行时代码仍通过固定低地址读取 v1 `BootInfo` 和 E820 ARDS，kernel entry 也没有显式接收 `BootInfo*`。现在需要先落地一个兼容的 handoff ABI 基础层，让 Legacy BIOS 路径继续可运行，同时为未来 UEFI loader、统一内存图、framebuffer 和 firmware table handoff 提供稳定接入点。

## What Changes

- 定义统一 boot handoff ABI 的可执行阶段：`BootInfoHeader + tagged sections`、少量固定 core 字段、section table metadata 和边界校验规则。
- 定义 `BootMemoryRegion` 统一内存区域格式，覆盖 physical base、length、normalized type、attributes 和固件来源 metadata。
- 将 Legacy BIOS E820 数据规范化并写入 v2 memory map section，供早期内存初始化通过统一 `BootMemoryRegion` view 消费；同时保留 v1 `BootInfo` 和低地址 E820 fallback。
- 将 kernel entry ABI 迁移为通过约定寄存器传递 `BootInfo*`，并保证 runtime `_start` 能把该指针转发给 `kernel()`。
- 保持现有 BIOS/MBR/exFAT/`boot.bin`/Bochs 启动路径和 `make boot-debug` 语义不变。
- 非目标：不实现 `BOOTX64.EFI`，不生成 ESP/FAT UEFI 镜像，不引入 QEMU/OVMF 启动入口，不支持 UEFI Runtime Services，不要求 kernel 消费 UEFI raw descriptor，不替换现有 Legacy BIOS loader。

## Capabilities

### New Capabilities

- `unified-boot-handoff-abi`: 定义并落地统一 boot handoff ABI 的初始运行时契约，包括 Legacy BIOS 生产完整 v2 handoff blob、寄存器传递 `BootInfo*`、统一 memory region view 和 v1 fallback。

### Modified Capabilities

- `x86-bootloader-hardening`: 扩展 Legacy BIOS backend 的 handoff producer 要求，使其在保持 v1 兼容的同时生产完整 v2 handoff blob、支持寄存器传递和统一内存图输入。

## Impact

- 影响子系统：x86 boot handoff ABI、runtime `_start`、kernel entry、早期内存初始化、公共 boot header 和启动架构文档。
- 可能修改文件：`include/arch/x86/boot/boot_info.h`、`src/arch/x86/boot/boot.s`、`src/runtime/crt0.s`、`src/kernel/kernel.cc`、`src/mm/buddy.cc`、`include/bigos/memory.h` 或相关 mm header、`docs/arch/x86-boot-layout.md`、`docs/arch/uefi-boot-blueprint.md`。
- 架构假设：目标仍是 x86_64 freestanding kernel；Legacy BIOS loader 已进入 long mode、开启分页并跳转到 higher-half ELF64 entry；未来 backend 也必须在进入 kernel 前准备等价环境。
- ABI 假设：x86_64 使用 System V 风格第一个参数寄存器 `rdi` 传递 `BootInfo*`；固定低地址 `BIGOS_BOOT_INFO_ADDRESS` 仅作为 Legacy fallback 和迁移期兼容。
- 内存布局假设：不移动现有 `0x0500` E820 buffer、`0x0800` legacy aliases、`0x0840` v1 `BootInfo`、boot-stage page table 区域或 kernel load base；新增 v2/section 数据不得破坏这些地址。
- 模拟器假设：`make boot-debug` 继续使用现有 Legacy BIOS/Bochs 路径；本 change 不要求 QEMU/OVMF。
- 磁盘布局假设：继续使用现有 MBR/exFAT raw image 和 `/boot/boot.bin`、root `kernel`；不新增 ESP 镜像。
- 工具链假设：继续使用 xmake 和 `x86_64-elf-gcc`/`x86_64-elf-g++`；辅助 clang/clangd 仅作为静态诊断信号。
