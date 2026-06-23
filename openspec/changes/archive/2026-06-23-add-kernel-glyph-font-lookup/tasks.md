## 1. Font Asset Format And Converter

- [x] 1.1 定义 glyph lookup asset 的 magic、format version、header size、table offset、glyph record、bitmap range、width class 和 flags，并保持公共结构 size/alignment/offset 可校验。
- [x] 1.2 将现有字体生成逻辑从 header 加原始 HEX 文本改为构建期解析随附点阵字体，输出紧凑二进制 lookup payload。
- [x] 1.3 在转换器中只收录 Unifont 8x16 半宽和 16x16 全宽 glyph，并由 bitmap 宽度推断 width class；其它尺寸、重复 codepoint、非法 bitmap 长度、乱序或重叠范围、越界 offset、glyph count 不一致和超过最大资产大小的输入必须确定性拒绝或诊断。
- [x] 1.4 确认生成产物仍写入 `build/assets/fonts/unifont.bin`，并由现有 UEFI ESP 打包路径放置到 `/boot/fonts/unifont.bin`。
- [x] 1.5 为转换器增加 focused Python 测试，覆盖有效半宽/全宽 glyph、缺字查询输入、非法源数据和 payload header/table 边界。

## 2. Boot Handoff And UEFI Loader

- [x] 2.1 更新 BootInfo font asset format 常量和 header 校验，使 loader/kernel 能区分新版 glyph lookup asset、缺失 asset 和不支持的 legacy/raw payload。
- [x] 2.2 调整 UEFI loader 的 font asset 基本门禁，校验 magic、header size、format version、declared byte size、glyph/cell metrics 和最大文件大小。
- [x] 2.3 保持 UEFI loader 只加载和描述 `/boot/fonts/unifont.bin`，不解析 codepoint range、不搜索 glyph record、不写 framebuffer pixel。
- [x] 2.4 确认 font asset unavailable fallback 不破坏 required BootInfo v2 sections、framebuffer metadata、memory map records 或 default bounded userland boot path。
- [x] 2.5 审查 BootInfo magic/version/header layout、kernel entry ABI、link address、page-table assumptions、IDT/syscall vector 和 CR3 switching 未被本 change 改变。

## 3. Kernel Glyph Lookup

- [x] 3.1 新增内核 glyph lookup view/API，接收 early font asset metadata 和保留的 payload byte range，暴露只读 bounded 查询接口。
- [x] 3.2 实现 payload header、range/index table、glyph record、bitmap offset、bitmap size、alignment 和 overflow 校验，失败时不暴露可用 lookup view。
- [x] 3.3 实现按 Unicode codepoint 查询 glyph bitmap slice、glyph/cell metrics 和 width class 的无分配路径。
- [x] 3.4 实现缺失或不支持 codepoint 的 deterministic missing-glyph/not-found 返回，不在 lookup 层做 framebuffer 渲染替代或 Legacy text 降级策略。
- [x] 3.5 确认 lookup 初始化不依赖文件系统、UEFI Runtime Services、动态分配、运行期 HEX 解析或 framebuffer device mapping。
- [x] 3.6 增加 source-level 或 focused 测试，覆盖有效 payload、缺字、越界 record、越界 bitmap、错误 version 和 fallback 可用性。

## 4. Documentation And OpenSpec Alignment

- [x] 4.1 更新英文架构文档，说明 glyph lookup asset 格式边界、构建期转换、ESP runtime path、UEFI loader 职责、kernel lookup view 和非目标。
- [x] 4.2 同步更新对应中文架构文档，保持与英文文档能力边界一致。
- [x] 4.3 更新验证记录模板或 notes，区分 glyph lookup asset readiness、framebuffer handoff readiness、default boot baseline 和未完成的 renderer/Unicode display。
- [x] 4.4 检查路线图和 OpenSpec 文案，确保不引用路线图任务编号，不宣称 framebuffer glyph rendering、Unicode display、software cursor、framebuffer scrollback、Secure Boot、ACPI handoff 或 UEFI Runtime Services 已完成。

## 5. Validation

- [x] 5.1 运行 OpenSpec 校验，确认 proposal、design、specs 和 tasks 均可解析且 apply-ready。
- [x] 5.2 针对字体转换和 source-level 检查运行 `uv run pytest`；如修改 Python 文件，同时运行 `uv run ruff check`、`uv run ruff format --check` 和 `uv run pyright`，不可用时记录 blocker。
- [x] 5.3 运行 xmake cross-toolchain build；若 x86_64-elf-gcc/x86_64-elf-g++、LLVM/LLD、xmake 或字体构建依赖不可用，记录 blocked/skipped、替代检查和残余风险。
- [x] 5.4 对新增或修改的 C++/header 路径运行接近 freestanding C++17/x86_64 配置的 clang 和 clangd 辅助诊断，区分历史诊断、当前 change 引入的问题和 freestanding false positives。
- [x] 5.5 运行 QEMU + OVMF headless UEFI smoke，验证 bounded userland baseline 仍达到，并记录 glyph lookup asset 生成、打包、loader 加载和 kernel 校验 evidence。
- [x] 5.6 运行或记录 Legacy BIOS fallback 验证，确认显式 Legacy path 不依赖 GOP、framebuffer metadata 或 glyph lookup font metadata。
- [x] 5.7 汇总 validation notes，明确 passed、failed、blocked/skipped、当前 change 诊断、历史诊断和仍未覆盖的图形人工验证风险。
