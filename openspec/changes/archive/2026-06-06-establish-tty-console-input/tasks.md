## 1. 梳理与骨架

- [x] 1.1 复核 `tmp/terminal-keyboard-wip/` 中可迁移的 scancode/keymap、控制字符和 TTY/console 片段，明确丢弃 `pty` 命名、ISR 直写输出和默认 unmask IRQ1 的旧做法。
- [x] 1.2 新增主线 keyboard/input、TTY、console 的头文件和源文件骨架，统一命名空间并保持 public headers 最小化。
- [x] 1.3 更新 `xmake.lua` 或源码列表，确保新增 C++ 文件纳入 freestanding kernel 构建且不引入 hosted runtime 依赖。

## 2. Keyboard 输入解码

- [x] 2.1 实现 PS/2 set-1 最小 scancode 解码表，覆盖可打印 US-layout 字符、Enter、Backspace、Tab、Escape 和必要控制键。
- [x] 2.2 实现 Shift/Ctrl/Alt 等基础 modifier make/break 状态更新，确保 modifier scancode 本身不产生普通字符输入。
- [x] 2.3 定义 unsupported/extended scancode 策略，确保未知输入可丢弃或记录且不会破坏 decoder 状态。

## 3. TTY 输入缓冲

- [x] 3.1 实现静态或初始化期固定容量 input ring buffer，支持 IRQ-context producer 和非中断 consumer。
- [x] 3.2 实现 buffer full 的确定性 overflow 策略，记录 drop counter 或明确丢弃新输入，不覆盖 unread input 除非文档声明。
- [x] 3.3 提供非阻塞读取/drain API，空 buffer 返回 empty result，不引入 sleep、wait queue 或 scheduler 依赖。

## 4. Console/TTY 输出

- [x] 4.1 实现最小 console/TTY 初始化、字符输出和字符串输出 API，普通输出经统一 console 层写 VGA text backend。
- [x] 4.2 实现 newline、carriage return、tab、backspace 的基础 text-mode 行为，并记录 unsupported escape sequence 策略。
- [x] 4.3 保留 early diagnostic direct-output path，确认 page fault、panic、memory self-test marker 不依赖 TTY 初始化。

## 5. IRQ 与 kernel 接入

- [x] 5.1 修改 keyboard IRQ1 handler，使其只读取 port `0x60`、执行 bounded decode/enqueue，并返回 dispatch 统一 EOI。
- [x] 5.2 确认 keyboard ISR 不调用 `kprintf`、`kput`、动态分配、阻塞等待、`mdelay()`、filesystem、scheduler、syscall 或用户态相关路径。
- [x] 5.3 更新 `kernel()` 初始化顺序，确保 TTY input buffer 和 keyboard state ready 后才允许 keyboard IRQ1 unmask。
- [x] 5.4 保持 `keyboard_smoke` 默认关闭；需要显式验证时再 unmask IRQ1，并确保默认 boot 不依赖键盘输入。

## 6. 测试与源码级检查

- [x] 6.1 新增或更新源码级测试，覆盖 keyboard IRQ1 handler 注册先于 unmask、handler 不直接 EOI、EOI 仍由 external IRQ dispatch 统一发送。
- [x] 6.2 新增 scancode/keymap 测试，覆盖代表性字母、数字、符号、Shift 组合、Enter、Backspace、Tab、Escape 和 unsupported scancode。
- [x] 6.3 新增 ring buffer 测试，覆盖 FIFO、empty read、full overflow/drop 策略和 producer/consumer 边界。
- [x] 6.4 新增 ISR 安全检查，确认 keyboard ISR body 不包含 `kprintf`、`kput`、allocation API、blocking wait 或 `mdelay()`。

## 7. 文档

- [x] 7.1 更新或新增 keyboard/TTY/console 架构文档，记录输入数据流、console 输出边界、初始化顺序、overflow 策略和非目标。
- [x] 7.2 更新中断/异常文档中 keyboard IRQ1 章节，从 smoke-only 描述扩展为受控输入 handoff，并保留 EOI/IRQ-context 约束。
- [x] 7.3 记录 `kput()`/`kputs()` 在TTY console input capability 保留 early direct output 语义，普通 runtime 输出使用新的 console API，COM1 仅用于 bounded marker/diagnostic。

## 8. 验证

- [x] 8.1 运行默认 `xmake` 或最窄可用交叉构建，确认新增 keyboard/TTY/console C++ 源码编译通过。
- [x] 8.2 运行 keyboard/TTY 相关源码级测试，例如 `uv run pytest tests/<keyboard_tty_test>.py`；若测试文件名不同，在 validation 中记录实际命令。
- [x] 8.3 运行 freestanding C++17 辅助语法检查，尽量贴近 cross GCC flags：`-ffreestanding -mno-red-zone -fno-rtti -fno-exceptions` 和项目 include paths。
- [x] 8.4 检查修改的 C++/header 文件 IDE diagnostics 或 clangd diagnostics，区分历史诊断、当前变更诊断和 freestanding false positive。
- [x] 8.5 在 Bochs/ROM/serial/VGA/manual keyboard oracle 可用时记录人工 keyboard smoke 操作步骤和结果；本阶段不扩展 `tools/boot_debug.py` 自动注入 scancode，若不可用则记录缺失依赖、已通过的 source/build 检查和剩余 runtime 风险。
- [x] 8.6 运行 `openspec validate establish-tty-console-input --strict` 并修复问题。
