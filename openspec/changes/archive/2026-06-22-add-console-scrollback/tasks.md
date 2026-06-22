## 1. Console/VGA 输出边界梳理

- [x] 1.1 审查 `kernel/core/terminal/console.cc`、`kernel/core/device.cc`、`kernel/drivers/video/vga.cc`、`kernel/core/bigos/io.cc` 和 `kernel/core/syscall/syscall.cc` 的输出调用点，明确 runtime console、early diagnostic、panic 和 COM1 serial marker 的边界。
- [x] 1.2 定义默认 VGA text console 的固定参数和不变量：80x25、文本 cell/attribute、光标范围、tab/backspace/newline/carriage-return 行为和越界处理策略。
- [x] 1.3 确认本 change 不修改 boot handoff、linker 地址、IDT/IRQ/syscall 向量、page-table layout、VGA 文本显存基址、磁盘布局或用户态 syscall ABI，并在实现备注或验证记录中保留该结论。

## 2. 自动上卷实现

- [x] 2.1 在 VGA text backend 或 console 输出层实现光标边界保护，确保字符输出和换行不会把硬件光标推进到 80x25 可见范围之外。
- [x] 2.2 实现自动上卷：当输出越过最后一行时，将可见行向上移动一行，清空最后一行，并保持正常 text attribute。
- [x] 2.3 覆盖基本控制字符行为，确认 newline、carriage return、tab、backspace 在首行、末行、行首、行尾和上卷后都保持确定性。
- [x] 2.4 保持 `kput()`/`kputs()` early diagnostic 边界可审计，不把 panic、早期 fault 或 fixed serial marker 强制依赖 scrollback 初始化。

## 3. Scrollback 状态和视口

- [x] 3.1 设计并实现 console-owned 256 行固定容量 scrollback state，使用静态或初始化期拥有的 bounded storage，记录容量、head/tail、当前逻辑光标和 viewport offset。
- [x] 3.2 将普通 runtime console 输出写入 scrollback state，并从该 state 渲染当前 80x25 可见窗口。
- [x] 3.3 实现 viewport 重绘函数，支持从历史窗口重绘 VGA text buffer，并保证不依赖用户态、文件系统、动态扩容或 hosted runtime。
- [x] 3.4 实现容量耗尽策略，确定性丢弃最旧历史，并保持 newest output、当前 viewport 和 TTY 输入状态不被破坏。
- [x] 3.5 实现底部 follow policy：viewport 位于最新输出时自动跟随；用户查看历史时新输出不破坏历史视口，并允许通过底部导航回到最新输出。
- [x] 3.6 明确 clear-screen 策略并实现：清空可见窗口、重置 viewport 到底部，并记录是否保留或丢弃历史行。

## 4. 键盘和 TTY 控制事件

- [x] 4.1 扩展 PS/2 set-1 decoder 对 `E0` 扩展序列的处理，识别 PageUp、PageDown、Home、End scrollback navigation key。
- [x] 4.2 扩展 `TerminalInputRecord`/`TerminalInputKind` 或等价 TTY 事件表示，使非字符控制事件不会伪装成 printable byte。
- [x] 4.3 保持现有 `read_char`、blocking read、TTY drain 和用户态 stdin 字符语义兼容，确保 scrollback navigation event 不作为异常 printable 字符泄漏。
- [x] 4.4 在非中断 terminal consumer 路径中消费 scrollback navigation event，调整 viewport 并触发有界整屏重绘。
- [x] 4.5 审查 keyboard IRQ1 路径，确认扩展键处理只做有界 decoder 状态更新和固定容量入队，不执行 VGA 重绘、动态分配、阻塞等待、文件系统访问、shell 策略或普通 console 输出。
- [x] 4.6 定义并实现 TTY buffer 满时 scrollback navigation event 的确定性处理，复用或记录现有 overflow counter 策略。

## 5. Source-level 测试和静态检查

- [x] 5.1 增加或更新源码级 pytest，覆盖 VGA/console 自动上卷、光标边界、最后一行 newline、最后一个 cell 输出、backspace/tab 和 clear-screen 策略。
- [x] 5.2 增加或更新源码级 pytest，覆盖固定容量 scrollback state、viewport 重绘、容量回绕、底部 follow policy 和历史视口收到新输出的行为。
- [x] 5.3 增加或更新源码级 pytest，覆盖 PS/2 `E0` 扩展键分类、PageUp/PageDown/Home/End 事件、unsupported extended scancode 安全处理和字符读取 API 兼容性。
- [x] 5.4 增加或更新 IRQ safety 源码检查，确认 keyboard IRQ handler 不直接调用 VGA redraw、`kprintf`/`kput`、allocation API、blocking wait、`mdelay()`、filesystem 或 shell/userland policy。
- [x] 5.5 运行相关 Python 测试，例如 `uv run pytest tests/test_tty_console_input_source.py` 以及本 change 新增/更新的目标测试；若 `uv` 不可用，记录 blocker、跳过原因和剩余风险。

## 6. 构建和 C++ 辅助诊断

- [x] 6.1 运行默认配置的窄构建或完整构建，例如 `xmake`，确认 runtime console/VGA/TTY 修改可通过 `x86_64-elf-*` 交叉工具链构建。
- [x] 6.2 运行键盘或 scrollback 相关配置构建；若新增默认关闭 smoke switch，则通过 `xmake f --<switch>=y && xmake` 验证该配置。
- [x] 6.3 运行接近交叉构建环境的 clang++ syntax-only 辅助检查，覆盖修改过的 `kernel/core/terminal/*.cc`、`kernel/drivers/video/vga.cc`、`kernel/core/device.cc` 和相关 headers，并记录历史诊断、当前 change 诊断和 freestanding false positive。
- [x] 6.4 运行或记录 clangd 辅助诊断；若 clangd 无法用等价 freestanding/x86_64 flags 配置，记录缺口和剩余风险。
- [x] 6.5 修复当前 change 引入的有效 build、clang 或 clangd 诊断；无法修复的历史问题必须与当前 change 诊断分开记录。

## 7. Emulator 行为验证

- [x] 7.1 在 QEMU headless 或 graphical 路径可用时运行默认 boot 或 userland smoke，确认普通 shell/stdout 输出路径没有回退；推荐优先使用 `uv run python tools/boot_debug.py run --emulator qemu --display none ...` 记录 serial marker。
- [x] 7.2 在 QEMU 或 Bochs 支持交互键盘和显示时，手工或脚本化验证长输出自动上卷、PageUp/PageDown 翻页、Home/End 跳转、回到底部和新输出 follow policy。
- [x] 7.3 若 QEMU、Bochs、ROM/display、disk image、交叉工具链或键盘交互不可用，记录 missing dependency、已替代执行的源码/构建检查和剩余 runtime interaction risk。

## 8. 文档和 OpenSpec 边界

- [x] 8.1 更新相关 docs/en 文档，描述默认 console 自动上卷、固定容量 scrollback、键盘翻页边界和非目标，避免宣称完整 terminal/ANSI/POSIX 兼容。
- [x] 8.2 同步更新对应 docs/zh 镜像文档，保持相对路径一致和语义一致。
- [x] 8.3 更新 headers 或源码注释中的 public contract，只暴露必要 API，避免把内部 stable role、scrollback 容量或实现细节变成用户态 ABI。
- [x] 8.4 运行 OpenSpec 校验和针对文档边界的搜索，确认 artifacts、docs 和 headers 没有引入完整 terminal、termios、多终端、伪终端或无限历史的误导性描述。

## Validation Notes

- `uv run pytest tests/test_tty_console_input_source.py tests/test_device_driver_framework_source.py` passed.
- `xmake` passed with the default configuration.
- `xmake f --keyboard_smoke=y && xmake && xmake f --keyboard_smoke=n` passed.
- `clang++ --target=x86_64-elf ... -fsyntax-only` passed for modified terminal/VGA/device sources.
- `clangd --check=kernel/core/terminal/console.cc --compile-commands-dir=.` loaded `compile_commands.json`; it reported Apple clangd ExtractFunction tweak failures only, with no source semantic diagnostic for this change.
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/serial.log --expect-serial-marker BIGOS_USER_EXEC` passed.
- Interactive PageUp/PageDown/Home/End viewport behavior was not manually validated because this run used QEMU headless `--display none`; remaining risk is limited to real keyboard/display interaction and is covered by source-level event/IRQ checks plus successful default boot smoke.
- `openspec validate add-console-scrollback --strict` passed.
- Boundary search for terminal overclaims found only explicit non-goal/prohibition wording in OpenSpec and docs.
