## Why

UEFI 已成为默认可运行启动 backend，但内核仍没有一个规范化的早期 framebuffer 描述可供后续图形控制台使用。现在需要把固件图形输出获取、线性 framebuffer 几何信息和早期字体资产引用纳入 BootInfo v2 handoff，作为后续 framebuffer 文本控制台与 Unicode 文本模型的输入边界。

## What Changes

- 扩展 x86_64 UEFI loader，使其通过固件图形输出选择并保留一个线性 framebuffer 模式。
- 扩展 BootInfo v2 tagged-section 模型，新增可校验的 framebuffer metadata section，描述物理基址、尺寸、stride、像素格式和写入属性。
- 在内核早期 handoff consumer 中解析 framebuffer metadata，并以只读启动描述的形式暴露给后续 console backend。
- 为早期字体资产增加 handoff 边界：以 `assets/fonts/unifont_all-17.0.04.hex` 作为源字体资产，生成 `build/assets/fonts/unifont.bin`，并打包到 ESP 的 `/boot/fonts/unifont.bin` 后由 loader 把字体资产位置、大小、格式版本传给内核，但不在本 change 中实现完整字体转换管线。
- 保持 Legacy BIOS 文本模式路径可运行且语义不变；没有 framebuffer section 时内核继续使用现有 VGA text/serial 诊断路径。
- 增加针对 handoff ABI、UEFI GOP 模式、framebuffer 内存保留和缺失依赖的验证记录。

## Capabilities

### New Capabilities

- `framebuffer-boot-handoff`: 定义固件图形输出到 BootInfo v2 framebuffer/font metadata 的早期启动握手能力。

### Modified Capabilities

- `unified-boot-handoff-abi`: 扩展 BootInfo v2 section 类型、校验规则和未知/可选 section 兼容语义，纳入 framebuffer 与字体资产 metadata。
- `x86-uefi-boot-backend`: 要求 UEFI backend 获取 GOP 线性 framebuffer，并在进入内核前生成对应 BootInfo v2 section。
- `uefi-default-boot-backend`: 要求默认 UEFI backend 在保持 bounded userland baseline 的同时记录 framebuffer handoff 验证结果，且不把 framebuffer console parity 误记为已完成。

## Impact

- 影响子系统：x86_64 UEFI loader、BootInfo v2 handoff ABI、内核早期启动 metadata consumer、早期诊断/console 选择边界、构建与 smoke 验证记录。
- 字体资产布局：源字体文件位于 `assets/fonts/unifont_all-17.0.04.hex`，构建生成产物位于 `build/assets/fonts/unifont.bin`，ESP 内运行时路径为 `/boot/fonts/unifont.bin`；UEFI loader 只消费 ESP 内打包后的字体路径，不直接依赖仓库源路径或 build 输出路径。
- 架构假设：仅覆盖 x86_64 UEFI GOP；Legacy BIOS 保持 VGA text fallback，不要求 VBE 或 BIOS 图形模式。
- 内存布局假设：framebuffer 作为 firmware/MMIO/device 类物理区域保留，不加入普通 RAM allocator；内核只消费 handoff 描述，不在本 change 中重排 kernel link address、direct map 或页表布局。
- 工具链和 emulator 假设：优先使用 QEMU + OVMF 验证 UEFI GOP handoff；缺少 OVMF、QEMU、LLVM/LLD、mtools 或 cross-toolchain 时必须记录为 blocked/skipped。
- 非目标：不实现 framebuffer glyph renderer、Unicode cell model、完整字体转换管线、Secure Boot、ACPI table handoff、UEFI Runtime Services、Legacy BIOS 图形 backend、完整 POSIX 或新存储/设备驱动。
