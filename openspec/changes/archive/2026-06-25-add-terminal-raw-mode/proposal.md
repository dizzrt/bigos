## Why

BigOS 当前默认 TTY/console 已能支撑 `/bin/sh` 的有界交互输入，但输入解释策略仍由内核固定决定：回显、Ctrl-C、EOF-like、退格和 scrollback 控制没有用户态可切换边界。后续 ANSI/VT 输入序列、简单 TUI 程序和 shell 行编辑需要一个明确的“canonical vs raw”输入归属模型。

本 change 引入有界 terminal raw mode 能力，让前台用户程序可以显式接管默认终端输入字节，同时保持现有 canonical 行为作为默认安全模式。

## What Changes

- 为默认控制台终端增加有界输入模式状态：canonical mode 与 raw mode。
- 增加最小 terminal mode syscall/libc wrapper，用于读取和设置默认终端模式；接口保持 BigOS-specific bounded subset，不实现完整 POSIX `termios`。
- canonical mode 保持当前行为：可阻塞 stdin、基础回显、line end、backspace/delete-like 反馈、EOF-like、interrupt-like 输入和 scrollback 控制。
- raw mode 改为逐字节/事件驱动交付：普通字符和支持的控制键尽快返回给用户态，不等待 Enter，不做普通回显，不自动把 Ctrl-C 转为 foreground group 信号，不把用户态应接管的导航键固定消费为 scrollback。
- 定义 `fork`、`execve`、进程退出和 foreground command 完成后的模式继承/恢复策略，避免 shell 因前台程序崩溃或退出而永久停留在 raw mode。
- 为后续 `add-vt-ansi-console` 提供输入归属边界：raw mode 下可向用户态交付更多 ANSI 导航键序列，canonical mode 下继续保持内核默认 scrollback 和基础 shell 体验。

非目标：

- 不实现完整 POSIX `termios` 结构、`tcgetattr`/`tcsetattr` 全量语义、baud rate、parity、serial line discipline、`VMIN/VTIME`、软件流控或 modem 控制。
- 不引入多 TTY、伪终端、`/dev/tty`、后台读写控制、完整 job control、完整 shell 行编辑或动态 terminal database。
- 不改变 `int 0x80` syscall ABI 的既有号位、IDT/IRQ 向量、boot handoff、页表布局、磁盘布局、kernel link 地址或 early diagnostic-only 输出路径。
- 不要求 ANSI/VT 输出解析；该能力仍由 `add-vt-ansi-console` 承载。

## Capabilities

### New Capabilities

- `terminal-raw-mode`: 定义默认控制台终端的有界 canonical/raw mode 状态、用户态控制接口、输入交付规则、生命周期恢复策略和验证要求。

### Modified Capabilities

- `minimal-terminal-abstraction`: 将默认终端边界从“不支持 termios/raw mode”调整为支持 BigOS-specific bounded terminal mode subset，同时继续排除完整 POSIX terminal。
- `tty-console-input`: 修改 TTY 输入消费契约，使 canonical/raw mode 决定控制字符、回显、scrollback 和逐字节交付策略。
- `process-session-terminal-control`: 约束 terminal mode 设置权限、foreground group 生命周期和前台程序退出后的模式恢复边界。
- `bounded-syscall-surface`: 扩展有界 syscall surface，追加默认终端模式查询/设置接口，保持 syscall ABI append-only。
- `user-libc-min`: 暴露最小 libc wrapper/header 常量，使简单静态用户程序可以切换和恢复 raw mode。

## Impact

- 影响子系统：`kernel/core/terminal` 的 TTY 输入消费、terminal mode state、keyboard event 转换；`kernel/core/syscall/syscall.cc` 的新增 syscall dispatch；`kernel/core/proc` 的 `fork`/`execve`/exit/reap 与 foreground group 恢复边界；`user/libc` wrapper 和必要头文件。
- 影响用户态：`/bin/sh` 仍默认运行在 canonical mode；未来 shell 行编辑或 TUI 程序可显式进入 raw mode 并在退出时恢复。
- 影响验证：需要 source-level 检查、默认 `xmake` 构建、用户态 smoke 程序或 userland smoke 覆盖模式查询/设置、raw byte delivery、Ctrl-C 不自动信号化、canonical 恢复。
- 架构假设：仍为单一默认控制台终端、单核有界路径；不要求 SMP、多终端、伪终端或 hosted libc。
- 内存布局假设：terminal mode state 是固定大小内核状态，不引入动态缓冲增长、无界输入历史或文件持久化。
- 模拟器与工具链假设：验证使用 `xmake`、`x86_64-elf-gcc/x86_64-elf-g++`，QEMU/Bochs 可用时运行 runtime smoke；Python 辅助命令使用 `uv run ...`。
