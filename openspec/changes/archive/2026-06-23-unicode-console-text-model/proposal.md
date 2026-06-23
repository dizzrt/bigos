## Why

Framebuffer console backend 已能把现有 byte/`char` cell 渲染为 glyph，但默认 runtime console 仍缺少 UTF-8 解码、Unicode codepoint cell 和双宽 cell 布局。现在需要把文本模型升级到可表达非 ASCII 字符的边界，使 UEFI framebuffer 路径能够显示 CJK 等 glyph，同时保持 Legacy VGA text fallback 的确定性降级行为。

## What Changes

- 将默认 runtime console 输出路径从 byte/`char` cell 升级为 UTF-8 输入解码、Unicode codepoint cell 存储和有界无效序列处理。
- 定义 console-owned cell 宽度策略：ASCII 与半宽 glyph 占一个 cell，全宽 glyph 占两个 cell，并为双宽字符的 trailing cell 保留确定性占位语义。
- 更新换行、自动上卷、scrollback、viewport 重绘、光标移动、backspace/clear 行为，使其按 codepoint cell 与双宽 cell 边界工作。
- 让 framebuffer backend 按 codepoint 查询 kernel glyph lookup，并在缺字、无效 codepoint 或布局无法容纳时优先使用 `U+FFFD` replacement glyph；`U+FFFD` 不可用时再确定性降级。
- 将 Tab 定义为严格推进到 4 列 tab stop，且推进过程必须尊重双宽 cell 与行尾 wrapping 边界。
- 让 Legacy VGA text backend 对非 ASCII codepoint 做确定性降级显示，继续保持 Legacy BIOS fallback 可运行。
- 保持早期 diagnostic-only 输出、COM1 marker、panic/fault 诊断和用户态 fd/syscall ABI 不变；普通 stdout/stderr 仍经默认 terminal/console sink。

## Capabilities

### New Capabilities

- `unicode-console-text-model`: 定义默认 runtime console 的 UTF-8 解码、Unicode codepoint cell、双宽 cell 布局、scrollback/viewport 行为和 backend 降级边界。

### Modified Capabilities

- `framebuffer-console-backend`: 将 framebuffer renderer 的输入边界从现有 `char` cell 扩展为 console-owned codepoint cell，并允许按 glyph width class 渲染双宽 cell。
- `minimal-terminal-abstraction`: 将默认 runtime console 的输出状态从 ASCII/byte-oriented cell 扩展为有界 Unicode text display，同时继续不声明完整 ANSI/VT、termios、多终端或完整 POSIX terminal。

## Impact

- 影响子系统：`kernel/core/terminal` 的 console/TTY 输出状态、scrollback/viewport、framebuffer console backend、VGA text backend fallback、kernel glyph lookup consumer、用户态 stdout/stderr 到默认 console 的普通输出路径。
- 架构假设：首版仍只覆盖 x86_64 BigOS 默认单一 runtime console；UEFI framebuffer 路径用于 glyph 显示，Legacy BIOS 路径继续作为 VGA text fallback。
- 内存布局假设：不改变 framebuffer 映射策略、kernel direct-map 假设、page-table layout、CR3 切换规则、kernel link address 或 BootInfo ABI；console 状态使用固定有界存储，不引入动态扩容 scrollback。
- 磁盘与启动假设：不改变 Legacy MBR/exFAT 路径、ESP 字体路径、UEFI loader 字体 handoff、用户态 ELF 包装、syscall vector `0x80` 或用户态 ABI。
- 工具链和 emulator 假设：优先用源码级检查、默认 `xmake` 构建、QEMU headless fallback smoke 和 QEMU/OVMF 图形验证组合验证；缺少 cross-toolchain、QEMU、OVMF 或图形能力时必须记录 skipped/blocked 与剩余风险。
- 非目标：不实现 ANSI/VT escape parser、locale、Unicode normalization、combining mark shaping、grapheme cluster、emoji/ambiguous-width 策略、字体 fallback 链、输入法、多终端、完整 POSIX terminal、完整 libc 宽字符/多字节 API、UEFI Runtime Services、Secure Boot、virtio/AHCI/NVMe 或 UEFI 与 Legacy 存储/设备 backend parity。
