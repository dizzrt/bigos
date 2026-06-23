## 1. Console Cell Model And UTF-8 Decoder

- [x] 1.1 梳理 `kernel/core/terminal/console.cc`、`include/bigos/console_render.h`、`kernel/core/terminal/console_render.cc` 和相关 tests，确认 byte/`char` cell、scrollback ring、viewport、cursor 与 backend 边界的现状。
- [x] 1.2 定义内部 Unicode console cell 结构，记录 codepoint、颜色和 single/leading/trailing/blank/replacement 等最小 cell role；保持固定 80x25 visible grid 和 256-line scrollback 容量。
- [x] 1.3 在 console sink 中实现固定大小 UTF-8 decoder 状态，支持有效 1-4 byte UTF-8、跨 `console_write()` 的不完整序列和非法序列 resync。
- [x] 1.4 为 overlong、surrogate、out-of-range、stray continuation、序列超长和 EOF/flush-like 未完成序列定义优先 `U+FFFD` 的 deterministic replacement 行为，确保不会污染后续输出。
- [x] 1.5 将 newline、carriage return、tab、backspace 与 printable codepoint 路径分离，避免控制字节被错误存入 visible Unicode cell。

## 2. Codepoint Cell Layout And Scrollback Semantics

- [x] 2.1 基于 kernel glyph lookup width class 和 fallback policy 实现单宽/双宽 cell 分类；缺字优先使用 `U+FFFD` glyph，不引入完整 Unicode width table、locale 或 shaping 依赖。
- [x] 2.2 实现 codepoint 写入、双宽 leading/trailing cell 保留、行尾双宽 wrap、line advance、自动上卷和 cursor 更新。
- [x] 2.3 更新 backspace 行为，使其按逻辑字符删除单宽或双宽 cell，并清理 orphan trailing/leading 状态。
- [x] 2.4 实现 Tab 严格推进到 4 列 tab stop，覆盖普通 cell、双宽相邻位置、行尾 wrapping 和自动上卷边界。
- [x] 2.5 更新 clear、PageUp/PageDown/Home/End、bottom-follow、历史视口收到新输出和 scrollback rollover 行为，确保 Unicode cell state 可确定性重绘。
- [x] 2.6 审查 fixed-capacity storage、整数边界、line slot 计算和 cursor 坐标，确认不引入动态 scrollback 增长或越界访问。

## 3. Render Backend Integration

- [x] 3.1 更新 internal `ConsoleRenderCell` 和 backend 接口以消费 codepoint cell 与 cell role，保持 public console/terminal API 不扩大。
- [x] 3.2 更新 VGA text backend：ASCII printable 直接显示，非 ASCII、缺字、双宽 leading 和 trailing cell 使用确定性 `?` 或 blank 降级。
- [x] 3.3 更新 framebuffer backend：按 codepoint 查询 glyph lookup，单宽 glyph 在一 cell 内绘制，双宽 leading glyph 在两 cell 像素范围内绘制，trailing cell 不独立查 glyph，缺字优先绘制 `U+FFFD` glyph。
- [x] 3.4 审查 framebuffer 双宽绘制的 bounds checking、stride/offset 计算、pixel range、cursor overlay 和 failure behavior，确保不越过 MMIO mapping。
- [x] 3.5 保持 framebuffer backend selection prerequisites、VGA fallback、COM1 serial diagnostics 和 early diagnostic-only `kput()`/`kputs()` 独立于 UTF-8 decoder。

## 4. Documentation And OpenSpec Alignment

- [x] 4.1 更新相关架构文档，说明默认 runtime console 的 UTF-8 解码、codepoint cell、双宽 cell 和 Legacy fallback 降级边界；如修改 `docs/en`，同步对应 `docs/zh`。
- [x] 4.2 更新源码注释和文档中此前声明“未实现 UTF-8/CJK/codepoint cell”的边界描述，避免与本 change 完成后的能力冲突。
- [x] 4.3 检查 roadmap、OpenSpec 和验证记录文案，确保不引用路线图任务编号，不宣称 ANSI/VT、termios、完整 Unicode terminal、locale、shaping、多终端、完整 POSIX 或 UEFI runtime/storage parity。

## 5. Source-Level Tests

- [x] 5.1 增加或更新源码级 tests，覆盖 UTF-8 decoder 的 valid ASCII、2/3/4 byte、跨 write partial、invalid continuation、overlong、surrogate、out-of-range 和 resync 行为。
- [x] 5.2 增加或更新源码级 tests，覆盖单宽/双宽分类、行尾双宽 wrap、trailing cell、backspace 删除、4 列 tab stop/control byte 和 scrollback rollover。
- [x] 5.3 增加或更新源码级 tests，覆盖 framebuffer backend 不独立拥有 Unicode layout，VGA backend 对非 ASCII 和 trailing cell 做确定性降级。
- [x] 5.4 所有 Python 相关测试通过 `uv run pytest ...` 执行；若 `uv` 不可用，记录 blocker、跳过原因和剩余风险。

## 6. Build And Runtime Validation

- [x] 6.1 运行 OpenSpec status/validate 检查，确认 proposal、design、specs 和 tasks 可被工具识别。
- [x] 6.2 运行默认 `xmake` 构建，确认 Legacy VGA fallback 和普通 bounded userland baseline 仍可构建。
- [x] 6.3 对新增或修改的 C++ 源/头文件运行尽量贴近 freestanding x86_64 C++17 配置的 clang 辅助检查，区分历史诊断、当前变更诊断和工具链配置缺口。
- [x] 6.4 对新增或修改的 C++ 源/头文件运行 clangd 辅助检查或记录不可用原因；clang/clangd 不替代 xmake cross-toolchain build。
- [x] 6.5 在 QEMU headless 可用时运行 Legacy/default fallback smoke，确认 ASCII 输出、用户态启动和非 framebuffer 路径未回归；不可用时记录缺失工具和剩余风险。
- [x] 6.6 在 QEMU + OVMF + framebuffer 图形验证能力可用时验证 UTF-8/CJK 样例、双宽 cell、软件光标、scrollback viewport 和 Legacy fallback 差异；不可用时记录 skipped/blocked 与剩余风险。
