## Why

UEFI framebuffer handoff 已经能把字体资产作为早期 metadata 传给内核，但该资产仍是带 header 的原始 Unifont HEX payload，内核没有可直接按 codepoint 查询的紧凑字形表。现在需要把字体资产从“可移交的文件”推进到“可被后续 framebuffer 文本后端消费的内核 glyph lookup”，为图形控制台和后续 Unicode 文本模型建立稳定输入边界。

## What Changes

- 新增构建期字体资产转换管线，把随附点阵字体转换为版本化、紧凑、边界可校验的 glyph lookup payload。
- 定义内核侧只读 glyph lookup 视图，支持按 Unicode codepoint 查询半宽与全宽字形，并返回确定性的缺字结果。
- 调整现有 boot font asset payload 格式，使 UEFI loader 仍只负责加载 ESP 内字体资产并传递 metadata，而不解析字形索引。
- 在内核早期 handoff consumer 之后增加字体资产 bounds、format、table layout 和 glyph bitmap 范围校验，避免 framebuffer renderer 未来消费未验证数据。
- 保持 Legacy BIOS/VGA text fallback、serial diagnostics、default userland boot baseline 和现有 BootInfo v2 required section 语义不变。
- 增加针对转换脚本、字体 payload 格式、内核 lookup 边界和文档同步的验证任务。

## Capabilities

### New Capabilities

- `kernel-glyph-font-lookup`: 定义构建期字体转换产物、内核只读 glyph lookup 视图、半宽/全宽字形覆盖、缺字语义和 bounds 校验边界。

### Modified Capabilities

- `framebuffer-boot-handoff`: 将早期 font asset metadata 的 payload 语义从“ESP-loaded 原始字体资产”收紧为“ESP-loaded 可校验 glyph lookup asset”，但保持 metadata handoff 与 fallback 语义。
- `x86-uefi-boot-backend`: 要求 UEFI backend 加载新版 glyph lookup 字体资产并继续只传递地址、大小、格式版本和度量 metadata，不在 loader 中执行 glyph lookup。
- `uefi-default-boot-backend`: 要求默认 UEFI 验证记录 glyph lookup 字体资产是否生成、打包、加载和被内核校验，同时不把 glyph rendering 或 Unicode display 误记为已完成。

## Impact

- 影响子系统：Python 构建/镜像 helper、字体资产格式、ESP 打包路径、x86_64 UEFI loader 的字体 header 校验、BootInfo v2 font metadata consumer、后续 framebuffer console 的输入边界、OpenSpec 与双语架构文档。
- 架构假设：仅覆盖 x86_64 当前 UEFI/Legacy 双 backend；glyph lookup 数据是 framebuffer console 的输入，不引入第二 ISA、SMP 或新的 display backend。
- 内存布局假设：字体资产由 loader 保留并通过 BootInfo v2 描述；内核只暴露只读 bounded view，不改变 kernel link address、direct map 假设、CR3 切换规则、IDT/syscall vector 或用户态 ABI。
- 字体格式假设：源字体继续来自随附 Unifont HEX 资产；构建产物必须包含固定 magic/version、glyph 度量、索引/范围信息和 bitmap 数据边界，足以在内核 freestanding 环境中无动态分配地查询。
- 工具链和 emulator 假设：字体转换通过 `uv run` 驱动的 Python helper 或 xmake 集成触发；运行期验证优先使用 QEMU + OVMF，缺少 QEMU、OVMF、mtools、LLVM/LLD、cross-toolchain、xmake 或 `uv` 时必须记录 blocked/skipped 和替代检查。
- 非目标：不实现 framebuffer glyph renderer、软件光标、framebuffer 滚动、UTF-8 解码、codepoint cell buffer、双宽 cell 布局策略、Legacy BIOS 图形模式、Secure Boot、ACPI handoff、UEFI Runtime Services、完整 POSIX 或新存储/设备驱动。
