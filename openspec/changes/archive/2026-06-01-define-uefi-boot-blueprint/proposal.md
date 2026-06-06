## Why

BigOS 当前启动链路完全围绕 Legacy BIOS/MBR/exFAT 设计，后续内存、显示、ACPI、磁盘和调试能力继续迭代时容易继续固化 BIOS 假设。现在先建立 UEFI 启动蓝图和项目级规划，可以让后续模块围绕统一 handoff 契约演进，避免未来主力转 UEFI 时大规模返工。

## What Changes

- 定义 UEFI 启动蓝图，明确 UEFI 作为未来并行 boot backend，而不是立即替换现有 BIOS 启动链路。
- 定义 BIOS 与 UEFI 共同收敛到统一 kernel handoff 的项目级原则：kernel 不直接依赖 BIOS interrupt 或 UEFI Boot Services。
- 规划 `BootInfo` 后续版本化演进方向，预留统一内存图、boot protocol、framebuffer、firmware tables、loader metadata 等字段。
- 将 `BootInfoHeader + tagged sections` 确认为长期 handoff 方向，并规划未来 kernel entry 通过寄存器传递 `BootInfo*`。
- 规划内存模块后续从 E820 兼容读取迁移到统一 `BootMemoryRegion` 视图，以便未来 UEFI `GetMemoryMap` 由 loader 规范化后交给 kernel。
- 明确 UEFI loader 后续单独实现适合 UEFI 的 ELF reader，但 BIOS 与 UEFI loader 共享 ELF64 加载规则规范。
- 明确近期不支持调用 UEFI Runtime Services，但统一 memory map 必须保留 runtime memory 类型和 attributes。
- 明确现阶段非目标：不实现 `BOOTX64.EFI`、不改 `make boot-debug` 语义、不替换现有 MBR/DBR/exDBR/`boot.bin` 路径、不在本 change 中修改当前运行时代码；同时规划未来 kernel entry ABI 改为寄存器传递 `BootInfo*`。
- 将现在不做但后续阶段计划要做的事项整理为项目级规划，包括 UEFI loader spike、ESP 镜像、OVMF/QEMU 调试入口、BootInfo v2 落地、内存模块迁移、GOP/ACPI 预留与验证策略。

## Capabilities

### New Capabilities

- `uefi-boot-blueprint`: 定义 UEFI 启动兼容蓝图、统一 handoff 原则、项目级阶段规划和对后续模块开发的兼容约束。

### Modified Capabilities

- `x86-bootloader-hardening`: 补充现有 BIOS boot hardening 的边界说明，要求 Legacy BIOS 路径在 UEFI 蓝图阶段继续保持兼容，并作为统一 handoff 的一个 producer。
- `one-command-boot-debug`: 补充启动调试命令规划，要求 `make boot-debug` 保持 Legacy BIOS 语义，未来 UEFI 调试入口使用独立命令。

## Impact

- 影响子系统：x86 boot、boot handoff ABI、内存初始化、未来显示/ACPI/调试入口规划。
- 直接产物：OpenSpec proposal/design/tasks/specs 和项目级规划文档任务；现阶段不修改运行时代码。
- 后续可能影响文件：`include/arch/x86/boot/boot_info.h`、`src/mm/buddy.cc`、`docs/en/arch/`、`tools/boot_debug.py`、顶层 `Makefile`、未来 UEFI loader 目录。
- 架构假设：目标仍是 x86_64 freestanding kernel，kernel 保持 higher-half ELF64；未来 boot backend 进入 kernel 时应已进入 long mode，分页和栈由 loader 准备，并通过寄存器传递 `BootInfo*`。
- 内存布局假设：现有 BIOS 固定低地址布局在蓝图阶段不破坏；未来 `BootInfo` 演进需要显式定义与旧版兼容关系。
- 模拟器假设：现有 Bochs BIOS 调试入口保留；未来 UEFI smoke test 优先规划 QEMU + OVMF，Bochs UEFI 支持作为可选验证路径。
- 磁盘布局假设：现有 MBR/exFAT raw image 继续服务 BIOS 路径；未来 UEFI 路径单独规划 ESP/FAT 镜像，不复用 `make boot-debug` 的语义。
- 工具链假设：现有 kernel 继续由 `x86_64-elf-gcc`/`x86_64-elf-ld` 构建为 ELF64；未来 UEFI loader 可能需要 PE/COFF 输出或转换流程，但不要求 kernel 本身改成 PE/COFF。
