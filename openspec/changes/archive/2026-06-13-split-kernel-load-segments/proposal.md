## Why

当前 `link.lds` 又把 kernel 链接成单个 `RWE PT_LOAD`，`xmake` 链接阶段持续报告 `LOAD segment with RWX permissions`。这与既有 `kernel-elf-segment-layout` 规格和架构文档中要求的 text/rodata/data 权限拆分不一致，也会掩盖后续内核 W^X 页权限收敛前的静态布局问题。

## What Changes

- 修复 kernel linker script，使 kernel ELF 重新生成独立的 `RX` text、`R` rodata 和 `RW` data/bss `PT_LOAD` program headers。
- 验证 Legacy BIOS bootloader 仍按 ELF64 program header 遍历所有 `PT_LOAD`，不会因多段 kernel ELF 遗漏加载、zero-fill 或 kernel extent 计算。
- 增加或恢复源码级/构建级检查，确保 `build/kernel` 不再包含同时 `W+X` 的 loadable segment，且链接阶段不再出现 RWX LOAD warning。
- 保持当前 higher-half base、`_start` entry、BootInfo handoff、MBR/exFAT/ATA PIO 启动路径、IDT/syscall vector、页表布局和运行时页权限策略不变。
- 不启用运行时内核 text/rodata/data 页级 W^X，不引入 ELF segment metadata handoff，不改变 UEFI 后端状态。

## Capabilities

### New Capabilities

无。该 change 是让当前实现重新满足既有 kernel ELF segment layout 和 x86 bootloader 规格，不新增独立能力族。

### Modified Capabilities

- `kernel-elf-segment-layout`: 修复实现以满足既有“无 RWX LOAD segment、text/rodata/data 分段权限、4KiB 边界、可复现验证”要求；如实现发现规格缺口，仅补充验证场景。
- `x86-bootloader-hardening`: 确认并验证 Legacy BIOS bootloader 对多 `PT_LOAD` kernel ELF 的既有加载契约仍成立；如发现回归，仅补充约束或测试覆盖。

## Impact

- Affected boot/kernel subsystem: x86_64 Legacy BIOS kernel ELF build artifact, top-level `link.lds`, boot image generation and bootloader ELF64 load validation.
- Affected validation: `xmake` build output, `x86_64-elf-readelf`/`objdump` inspection, OpenSpec validation, and at least one QEMU/Bochs boot smoke when local emulator/ROM/display/serial oracle is available.
- Architecture assumptions: kernel remains higher-half at `0xffffffff80000000`; `_start` remains entry; `PT_LOAD` virtual addresses stay page-aligned and above the higher-half base.
- Memory/layout assumptions: this change affects ELF program header permissions only; it does not change active kernel page-table permissions, direct map layout, CR3 switching, boot info address/pointer ABI, or runtime memory allocator initialization.
- Disk/emulator/toolchain assumptions: validation depends on `x86_64-elf-*`, xmake, image generation, QEMU/Bochs availability, Bochs ROM/display configuration, and serial marker observation.
