## Why

当前运行时 console/TTY 已具备 UTF-8、Unicode cell、scrollback、默认 shell I/O 和 framebuffer/VGA 后端基础，但 ANSI/VT escape 仍被明确排除。继续增强用户态可用性时，彩色输出、清屏、光标移动和方向键等常用终端协议已经成为 `/bin/sh`、简单 TUI 程序和调试工具的基础能力。

本 change 将按稳健路线引入有界 ANSI/VT 支持：先把 console 状态升级为 VT-ready，再实现常用输出侧 ANSI/CSI 子集，最后让输入侧按键能向用户态暴露确定性的转义序列。

## What Changes

- 将运行时 console 模型扩展为 VT-ready：保留固定容量 scrollback/viewport，同时增加可寻址 visible screen 状态、当前显示属性、光标状态和有界 escape parser 状态。
- 支持输出侧常用 ANSI/CSI 子集，包括 SGR 颜色/重置、光标移动、光标定位、擦除屏幕、擦除行、保存/恢复光标，以及确定性无效序列处理。
- 将 console cell 的颜色从固定属性扩展为由当前 SGR 属性驱动，并保持 VGA text backend 与 framebuffer backend 的降级/渲染边界。
- 让输入侧把方向键、Home/End、Delete、PageUp/PageDown 等默认终端导航按键按有界策略转换为用户态可见的 ANSI 转义序列，交给当前前台用户程序消费；`Shift+PageUp`/`Shift+PageDown` 保留为内核 console scrollback 快捷键；既有 BigOS-specific raw mode 作为用户程序直接消费这些序列的模式边界。
- 更新文档与验证，明确支持的是 bounded VT/ANSI subset，而不是完整 xterm、termios、伪终端或完整 POSIX terminal。

非目标：

- 不实现完整 xterm/VT100/VT220 兼容矩阵。
- 不实现 OSC、DCS、APC、PM、复杂 DEC private modes、alternate screen、鼠标协议、bracketed paste 或完整功能键矩阵。
- 不引入 POSIX `termios`、伪终端、多 TTY、`/dev/tty`、shell 命令历史或完整行编辑；已有 BigOS-specific canonical/raw mode 不扩展为完整 `termios`。
- 不改变 `int 0x80` syscall ABI、IDT/IRQ 向量、页表布局、boot handoff、磁盘布局、kernel link 地址或 early diagnostic-only 输出路径。

## Capabilities

### New Capabilities

- `vt-ansi-console`: 定义 BigOS 默认运行时文本控制台支持的有界 ANSI/VT 输出解析、显示属性、光标/擦除操作、输入侧按键转义序列和非目标边界。

### Modified Capabilities

- `tty-console-input`: 将“ANSI/VT 不解析”的最小 console/TTY 行为改为支持有界输出解析和输入侧按键转义序列，同时保持 IRQ-safe keyboard producer 与默认 shell I/O 边界。
- `unicode-console-text-model`: 将 Unicode cell 模型扩展为携带当前显示属性的 VT-ready cell，并要求 UTF-8 解码与 escape parser 有明确优先级和失效恢复策略。
- `framebuffer-console-backend`: 允许 framebuffer backend 渲染带显示属性的 console-owned cells，同时保持其 renderer-only 边界，不拥有 VT 状态。

## Impact

- 影响子系统：`kernel/core/terminal` 的 console、TTY、keyboard decode、console render backend；`include/bigos/console*.h`、`include/bigos/tty.h` 的最小接口；`kernel/core/syscall/syscall.cc` 的默认 fd `0/1/2` console fast path行为边界。
- 影响用户态：`user/sh/sh.c` 和简单 `/bin/*` 可以继续通过 `write/read` 使用默认 console；可新增小型测试程序输出 ANSI 序列验证常用行为。
- 影响验证：需要 source-level parser/TTY 检查、默认 `xmake` 构建、必要的 clang/clangd 辅助诊断，以及 QEMU/Bochs 中至少一种可用图形 console 或 headless marker 辅助验证。
- 架构假设：目标仍为 x86_64 freestanding BigOS，默认单核路径；不要求 SMP、UEFI Runtime Services、hosted libc 或动态终端数据库。
- 内存布局假设：继续使用固定容量 console state；不引入无界 scrollback、堆增长依赖或用户可映射 framebuffer。
- 模拟器与磁盘假设：Legacy BIOS/VGA 路径必须保持可运行；UEFI framebuffer 路径在 prerequisites 满足时可验证，但本 change 不改变启动镜像布局。
- 工具链假设：实现与验证使用 `xmake` 和 `x86_64-elf-gcc/x86_64-elf-g++`；Python 辅助验证如需运行必须通过 `uv run ...`。
