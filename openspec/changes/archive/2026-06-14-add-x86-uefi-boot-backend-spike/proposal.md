## Why

`roadmap.md` 的 Backend 扩展试探阶段要求在不引入第二 ISA 的前提下选择一个真实 backend 扩展来验证前序架构边界。当前 Legacy BIOS/MBR/exFAT 路径已形成可运行基线，适合以低风险方式引入 x86_64 UEFI boot backend spike，验证 boot handoff、内存图规范化、镜像生成和 QEMU/OVMF 调试入口是否能与现有内核运行时解耦。

## What Changes

- 新增一个 x86_64 UEFI boot backend spike，生成独立的 `BOOTX64.EFI`，通过 QEMU + OVMF 从 ESP/FAT 镜像启动。
- 新增 UEFI loader 的最小构建链路，优先使用本地 Homebrew `clang`、`lld-link`、`llvm-objcopy/llvm-objdump` 和最小 UEFI ABI/header/glue，不 vendor 完整 edk2，也不依赖不可用的 `gnu-efi`。
- 新增独立 ESP/FAT 镜像生成路径，使用 `mtools` 放置 UEFI loader、kernel ELF、默认 init 和 `/bin/*` 用户态程序。
- 新增独立 QEMU/OVMF 启动调试入口和串口日志路径，默认验收目标达到 Legacy BIOS 默认 headless 路径相同的 init/user exec marker，保留现有 Legacy BIOS/MBR/exFAT 的 QEMU/Bochs 路径不变。
- UEFI loader 通过现有 `BootInfo v2` 风格向 kernel 传递 boot protocol、core metadata、UEFI memory map 转换后的 `BootMemoryRegion` 视图、可选 storage metadata 和可选 loader metadata。
- 更新双语架构文档，记录 UEFI spike、metadata sections、工具链假设和 Legacy/UEFI 调试入口边界。
- 保持非目标明确：不新增第二 ISA，不启用 SMP，不实现 Secure Boot，不实现 GOP framebuffer，不调用 UEFI Runtime Services，不新增完整 POSIX、动态链接、广泛 file-backed `mmap` 或新存储驱动。

## Capabilities

### New Capabilities

- `x86-uefi-boot-backend`: 覆盖 x86_64 UEFI loader、ESP/FAT 镜像、QEMU/OVMF 调试入口、UEFI memory map 到 `BootMemoryRegion` 的转换、以及与现有 kernel handoff ABI 的集成边界。

### Modified Capabilities

- `uefi-boot-blueprint`: 将原先“只规划、不实现”的 UEFI 蓝图推进到可运行 spike，并保留后续正式化边界。
- `unified-boot-handoff-abi`: 增加 UEFI backend 生产 `BootInfo v2` handoff blob 的 requirement，不改变 Legacy BIOS fallback 兼容性。
- `one-command-boot-debug`: 增加独立 UEFI/QEMU/OVMF 调试入口和 ESP 产物隔离 requirement，不改变现有 Legacy BIOS 调试入口语义。

## Impact

- 受影响子系统：`kernel/arch/x86/boot`、`include/arch/x86/boot`、`xmake` 构建目标、`tools/boot_debug.py` 镜像/启动 helper、以及 boot/debug 文档。
- 新增本地工具假设：QEMU 11.x 或兼容版本、x86_64 edk2/OVMF code 固件、可复制的 OVMF vars template、Homebrew LLVM `clang`、`lld-link`、`llvm-objcopy/llvm-objdump`、`mtools`。
- 架构假设：仍为 x86_64 ISA；UEFI backend 负责进入 64-bit UEFI app 环境并加载现有 x86_64 higher-half kernel，不引入 AArch64/RISC-V 等第二 ISA。
- 内存布局假设：kernel link/load 约定、higher-half entry、`BootInfo v2` ABI、页表与早期内存初始化语义必须保持可校验；UEFI memory descriptors 进入 kernel 前被规范化。
- 模拟器假设：UEFI smoke 首选 QEMU + OVMF；Apple Silicon 主机可通过 TCG 运行 x86_64 QEMU，性能较慢但满足 bounded smoke。
- 磁盘布局假设：UEFI 路径使用独立 ESP/FAT 镜像；Legacy BIOS 路径继续使用现有 MBR/exFAT raw image，并在 runtime parity 明确前保持可运行。
