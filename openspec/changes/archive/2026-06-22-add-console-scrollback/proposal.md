## Why

当前默认 VGA 文本 console 只把字符写入 80x25 可见文本缓冲区，输出超过屏幕范围后没有自动上卷和历史回看能力，用户在 shell 或内核诊断输出较长时会丢失上下文。该问题已经影响默认交互体验，且现有 `kernel/drivers/video/vga.cc` 中 `scroll_screen` 仍是 TODO，适合以一个有界 terminal 能力补齐。

## What Changes

- 为 VGA 文本 console 增加自动上卷：当换行或字符输出越过最后一行时，屏幕内容向上滚动，最后一行清空，光标保持在可见区域内。
- 为默认 console 增加固定 256 行 scrollback 历史：普通 console 输出进入有界历史缓冲，可通过视口偏移重绘当前 80x25 可见窗口。
- 扩展 PS/2 set-1 键盘解码与 TTY/console 控制事件，支持 PageUp/PageDown/Home/End 有界翻页和跳转输入，用于调整 console scrollback 视口。
- 保持普通 stdout/stderr、早期诊断、panic、COM1 serial marker 和现有 shell 输入路径的边界稳定。
- 明确非目标：不实现完整 ANSI/VT100 终端仿真、`termios`、多终端、伪终端、后台读写控制、完整 job control、图形模式 console、动态分配的无限历史或用户态可见的新 terminal ABI。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `minimal-terminal-abstraction`: 扩展默认 console 输出契约，要求 VGA 文本 console 支持有界自动上卷、固定容量 scrollback、视口重绘和键盘翻页控制，同时保持 bounded terminal 非目标。
- `tty-console-input`: 扩展键盘/TTY 输入事件契约，要求支持 PageUp/PageDown/Home/End 等非字符控制键的有界传递，不破坏现有 ASCII 输入、阻塞读取和 IRQ 安全边界。

## Impact

- 影响子系统：`kernel/core/terminal/**`、`kernel/drivers/video/vga.cc`、`include/bigos/console.h`、`include/bigos/tty.h`、`include/bigos/keyboard.h`、`include/drivers/video/vga.h`，以及必要的源码级测试和文档。
- 架构假设：默认目标仍是 x86_64 Legacy BIOS VGA text mode，文本模式为 80x25，VGA 文本显存基址和现有端口 I/O 假设保持不变。
- 内存假设：scrollback 使用静态或初始化期拥有的 256 行固定容量缓冲，不依赖堆动态扩张，不在 IRQ 路径分配内存。
- 中断假设：键盘 IRQ1 只做有界 scancode 解码和事件入队；实际视口重绘和屏幕滚动在非中断 console/terminal 路径完成。
- 启动和 ABI 假设：不改变 boot handoff、linker 地址、IDT/IRQ/syscall 向量、page-table layout、磁盘布局、用户态 syscall ABI 或现有 stdout/stderr fd 语义。
- 工具链/仿真假设：继续使用 xmake 与 `x86_64-elf-*` 交叉工具链；自动化验证优先源码级 pytest/构建检查，交互翻页行为在 QEMU/Bochs 可用时记录手工或脚本化 smoke。
