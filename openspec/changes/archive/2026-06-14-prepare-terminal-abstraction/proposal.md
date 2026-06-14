## Why

当前默认用户态已经具备可交互 shell、键盘输入和控制台输出，但终端相关语义仍散落在 TTY/console/shell 路径中，缺少面向后续交互能力的最小抽象边界。现在需要先整理有界输入归属、控制字符和用户可见终端行为契约，为后续交互增强提供稳定基础，同时避免提前承诺完整 POSIX terminal。

## What Changes

- 引入最小终端抽象能力，明确默认控制台终端在有界用户态中的输入归属、输出归属和状态边界。
- 为 shell 与简单用户程序需要的控制字符行为建立有界契约，例如行结束、退格、EOF/interrupt 类控制输入的可观察语义。
- 收紧 TTY/console 与用户态 fd I/O 的边界，使普通用户程序通过既有 stdin/stdout/stderr 路径观察终端行为，而不是依赖硬件或 diagnostic-only 输出。
- 保留 IRQ-safe keyboard producer 与非中断 consumer 边界，不把普通回显、控制字符解释或 shell 策略塞入 IRQ handler。
- 明确 non-goals：不实现完整 terminal control、termios、session、job control、process group、伪终端、SMP、动态链接、完整 POSIX shell 或完整 POSIX terminal 支持。

## Capabilities

### New Capabilities
- `minimal-terminal-abstraction`: 定义默认控制台终端的最小抽象、输入/输出归属、控制字符语义和边界。

### Modified Capabilities
- `tty-console-input`: 将既有键盘 TTY 输入和控制台输出要求扩展为终端抽象的底层 producer/consumer 边界，要求控制字符处理保持有界且非 IRQ。
- `user-shell`: 要求默认 shell 消费最小终端语义，包括有界行输入反馈、控制字符结果和错误展示。
- `runtime-smoke-validation`: 要求终端准备工作保留行为导向验证入口，并区分可自动化串口/headless 检查与需要人工控制台观察的交互风险。

## Impact

- Affected subsystems: kernel terminal/TTY/console path, keyboard input handoff, syscall fd/VFS I/O surface, userland `/bin/sh`, minimal user libc wrappers where needed, and runtime validation notes.
- Architecture assumptions: 当前可运行 backend 仍是 x86_64 与现有 Legacy BIOS/MBR/exFAT 路径；不引入 UEFI runtime parity、第二 ISA backend 或 SMP。
- Memory/layout assumptions: 不改变 kernel link address、page-table layout、direct map、syscall vector、CR3 switching contract、GDT/TSS、user/kernel ABI 或 boot handoff layout。
- Emulator/toolchain assumptions: 验证仍以现有 xmake 与 `x86_64-elf-gcc` 交叉工具链为基础，优先使用 QEMU headless 串口/日志检查；图形 QEMU/Bochs 控制台观察作为环境允许时的补充。
- Disk/storage assumptions: 继续使用现有 boot packaging、exFAT 启动资产和有界 `/rw` 运行时存储；不引入持久完整可写文件系统、广泛 file-backed mapping、async I/O 或新存储驱动。
