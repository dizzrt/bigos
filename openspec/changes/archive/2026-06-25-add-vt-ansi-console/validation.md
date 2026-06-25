# Validation Notes

## 实现前不变量

- `kernel/core/terminal/console.cc` 在本变更前已拥有固定 256 行 `ConsoleRenderCell` scrollback、`oldest_line/current_line/viewport_top` viewport 模型、`cursor_x` 横向光标、固定容量 `Utf8Decoder`、双宽 cell role，以及通过 `console_render_backend()` 完整重绘 viewport 的路径。
- 普通输出入口仍是 `terminal::default_terminal_write()` -> `console_write()` -> `console_put()`；early diagnostic-only 路径 `kput()`、`kputs()`、panic/serial marker 不经过 runtime console parser。
- render backend 在本变更前已经是 renderer-only：VGA backend 写 text cell，framebuffer backend 依据 console-owned codepoint/role 和 glyph lookup 绘制像素，不保存 UTF-8、scrollback、TTY 或 terminal mode 状态。
- `kernel/core/terminal/keyboard.cc` 的 IRQ producer 只做 PS/2 set-1 解码并调用 `terminal::enqueue_input_record()`；`kernel/core/terminal/tty.cc` 在非中断 consumer 中处理 canonical/raw mode、Ctrl-C、EOF-like、backspace 和 scrollback control。
- `kernel/core/terminal/keyboard.cc` 已跟踪 Shift/Ctrl/Alt make/break 状态；本变更将默认方向键、Home/End、Delete、PageUp/PageDown 交给用户态序列，只有 `Shift+PageUp`/`Shift+PageDown` 保留为内核 scrollback control。

## Source Tests 覆盖点

- `tests/test_tty_console_input_source.py` 覆盖 keyboard IRQ 安全边界、PS/2 set-1 映射、TTY ring 固定容量、canonical/raw mode、导航键序列展开、`Shift+PageUp/PageDown` 不泄漏 stdin、console VT parser、SGR、cursor、erase、save/restore 和 UTF-8/parser 交错边界。
- `tests/test_framebuffer_boot_handoff_source.py` 覆盖 framebuffer renderer-only 边界、动态 grid、glyph fallback、属性映射和映射范围检查。
- `tests/test_bilingual_docs_layout.py` 覆盖 `docs/en` 与 `docs/zh` 镜像路径一致性。

## 已通过检查

- `uv run pytest tests/test_tty_console_input_source.py tests/test_framebuffer_boot_handoff_source.py`
  - 结果：24 passed。
- `uv run pytest tests/test_bilingual_docs_layout.py tests/test_tty_console_input_source.py tests/test_framebuffer_boot_handoff_source.py`
  - 结果：28 passed。
- `xmake`
  - 结果：build ok。
- `clang++ -target x86_64-elf -std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/terminal/console.cc kernel/core/terminal/console_render.cc kernel/core/terminal/tty.cc kernel/core/terminal/keyboard.cc`
  - 结果：通过，无当前变更新增诊断。
- `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/add-vt-ansi-console-user-exec-serial.log --expect-serial-marker BIGOS_USER_EXEC`
  - 结果：QEMU headless smoke 观察到 `BIGOS_USER_EXEC`。
- `openspec validate add-vt-ansi-console --strict`
  - 结果：Change valid。

## 无法完成或降级的检查

- `clangd --check=kernel/core/terminal/console.cc --compile-commands-dir=.` 成功加载 `compile_commands.json` 并完成 AST 检查，但 Apple clangd 21 以 `tweak: ExtractFunction ==> FAIL: Cannot extract break/continue without corresponding loop/switch statement` 报 17 个 check-mode tweak 错误并返回 3。该输出未显示源代码语法/类型诊断；以 `clang++ -fsyntax-only` 和 `xmake` 作为当前变更的语法/构建证据。
- 未执行图形 QEMU/Bochs 人工或半自动观察。当前 agent 会话没有可交互图形显示与键盘注入观察通道；已用 source-level parser/TTY 检查和 QEMU headless serial marker 证明默认用户态仍可启动。剩余风险是颜色像素、光标定位和实际键盘导航序列的人工可用性仍需在可交互图形 emulator 中复核。

## 历史诊断与剩余风险

- QEMU smoke 的 UEFI 构建阶段保留历史 warning：`clang: warning: argument unused during compilation: '-ffreestanding'`。
- boot artifacts 构建阶段保留历史 assembler warning：`found 'movsd'; assuming 'movsl' was meant`。
- 当前变更新增的主要剩余风险集中在图形可观察行为：SGR 颜色与 VGA bright background 的硬件降级、framebuffer 像素色彩观感、以及真实键盘 Page/arrow 输入在 emulator 前端中的交互体验。
