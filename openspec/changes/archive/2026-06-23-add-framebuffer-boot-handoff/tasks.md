## 1. BootInfo ABI

- [x] 1.1 在公共 boot handoff header 中新增 framebuffer metadata section type、font asset metadata section type、像素格式枚举和属性 flag。
- [x] 1.2 定义紧凑的 framebuffer metadata 结构，覆盖物理基址、字节大小、宽高、stride、像素格式、bits/bytes-per-pixel 和 cache/write 属性。
- [x] 1.3 定义紧凑的 font asset metadata 结构，覆盖资产地址或来源、字节大小、格式版本、字形/单元度量和来源 flag。
- [x] 1.4 为新增结构补齐 size、alignment 和关键字段 offset 的构建期校验，确认 BootInfo v2 magic、header layout、entry ABI 和 v1 fallback 未变化。
- [x] 1.5 扩展 BootInfo v2 optional section 校验辅助函数，使 consumer 能区分 framebuffer/font metadata 的 valid、absent 和 invalid 状态。

## 2. UEFI Loader

- [x] 2.1 在 UEFI header/loader 支持中补齐 Graphics Output Protocol 所需的最小结构、GUID 和像素格式定义，保持 freestanding-safe。
- [x] 2.2 在 UEFI loader 中定位 GOP，接受 firmware 当前默认模式，并读取 framebuffer base、size、resolution、pixels-per-scanline 和 pixel format。
- [x] 2.3 将 GOP pixel format 或 bitmask 规范化为 BigOS framebuffer pixel format，unsupported format 走显式失败或无 framebuffer fallback。
- [x] 2.4 将 framebuffer metadata 写入 BootInfo v2 optional section，并保持 required core/memory map、storage metadata、loader metadata 的既有生成语义。
- [x] 2.5 确认源字体资产位于 `assets/fonts/unifont_all-17.0.04.hex`，构建生成产物位于 `build/assets/fonts/unifont.bin`，并在打包阶段复制到 ESP 的 `/boot/fonts/unifont.bin`。
- [x] 2.6 在 UEFI loader 中从 ESP 路径 `/boot/fonts/unifont.bin` 加载首个字体资产，使用 bounded size 和格式版本校验，并把 loader-provided 地址、大小、度量和 flag 写入 font asset metadata。
- [x] 2.7 确认 ESP 字体资产 buffer 在 `ExitBootServices` 后仍被保留，且 font metadata 无效时不会阻塞默认启动 fallback。
- [x] 2.8 在 loader 诊断中记录 framebuffer/font handoff 的关键字段和失败阶段，避免把 metadata 生成成功误当成图形 console 就绪。

## 3. Kernel Early Consumption And Memory Boundaries

- [x] 3.1 在内核早期 handoff consumer 中解析 framebuffer metadata，保存 immutable optional view，缺失时保持 VGA text/serial fallback。
- [x] 3.2 在内核早期 handoff consumer 中解析 font asset metadata，保存 immutable optional view，并在暴露前完成 bounds 和版本校验。
- [x] 3.3 审查并调整早期内存初始化，确保 framebuffer 物理范围不会进入 ordinary RAM free pool。
- [x] 3.4 建立最小 device/MMIO mapping API，接收物理基址、大小和属性，作为后续 framebuffer 写入的唯一入口。
- [x] 3.5 增加 source-level 检查或 focused test，覆盖 invalid framebuffer/font section 不会导致内核写 framebuffer 或破坏 fallback。
- [x] 3.6 增加 source-level 检查，确认 framebuffer console 相关代码不得直接通过 ordinary-RAM direct-map alias 写 framebuffer。
- [x] 3.7 确认 framebuffer handoff 和 device/MMIO mapping API 不改变 kernel link address、direct map 假设、CR3 切换规则、IDT/syscall vector 或用户态 ABI。

## 4. Documentation And OpenSpec Alignment

- [x] 4.1 更新英文架构文档，说明源字体资产路径 `assets/fonts/unifont_all-17.0.04.hex`、生成产物路径 `build/assets/fonts/unifont.bin`、ESP 运行时路径 `/boot/fonts/unifont.bin`、UEFI GOP framebuffer metadata、ESP-loaded font metadata、device/MMIO mapping API、Legacy fallback、memory reservation 和非目标边界。
- [x] 4.2 同步更新对应中文架构文档，保持与英文文档相同的能力边界和非目标。
- [x] 4.3 更新验证记录，区分 default UEFI boot baseline、framebuffer handoff 成功/失败、fallback、blocked/skipped dependency 和残余风险。
- [x] 4.4 检查路线图和 OpenSpec 文案，确保不宣称 glyph rendering、Unicode display、Secure Boot、ACPI handoff、Runtime Services 或完整设备/存储 parity 已完成。

## 5. Validation

- [x] 5.1 运行 targeted source/spec 检查，确认新增 BootInfo section、结构体 layout 和 parser 路径与规范一致。
- [x] 5.2 运行 xmake cross-toolchain build；若 x86_64-elf-gcc/x86_64-elf-g++、LLVM/LLD 或 xmake 不可用，记录 blocker 和残余风险。
- [x] 5.3 对修改过的 C++/header 路径运行接近 freestanding C++17/x86_64 配置的 clang 和 clangd 辅助诊断，区分历史诊断与当前 change 引入的问题。
- [x] 5.4 运行 QEMU + OVMF headless UEFI smoke，验证 bounded userland baseline 仍达到，并记录 framebuffer handoff metadata evidence。
- [x] 5.5 运行或记录 Legacy BIOS fallback 验证，确认显式 Legacy path 不依赖 GOP、framebuffer metadata 或 font metadata。
- [x] 5.6 如涉及 Python helper 修改，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright` 和 `uv run pytest`；若未修改 Python 文件，则在验证记录中标注不适用。
- [x] 5.7 汇总 validation notes，明确 passed、failed、blocked/skipped、当前 change 诊断、历史诊断和仍未覆盖的图形人工验证风险。
