## Why

当前默认用户态已经能启动 resident init 并进入 `/bin/sh`，但默认运行时文本控制台仍未被明确提升为可见、可操作的交互入口。Stage 20 需要把有界 shell 的 prompt、键盘输入回显和命令输出整理成项目级可用性目标，同时继续保留串口/日志可验证路径，避免把交互体验推进为完整 POSIX terminal。

## What Changes

- 将默认运行时文本控制台定义为有界用户态 shell 的用户可见交互路径。
- 要求 `/bin/sh` 在交互式 stdin/stdout 下显示有界 prompt、读取键盘输入、执行命令并把结果输出回控制台。
- 要求输入回显从非中断消费路径产生，保持键盘 IRQ1 和 TTY producer 的有界、IRQ-safe 属性。
- 保留默认/自动化验证所需的串口和日志观察能力，使 headless smoke 仍能判断核心用户态路径。
- 明确 non-goals：不引入完整 POSIX terminal、termios、作业控制、terminal process group、SMP、新 boot backend 或新架构运行时等价能力。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `tty-console-input`: 明确默认运行时文本控制台可作为用户态 shell 的可见 I/O 路径，并要求输入回显不破坏 IRQ-safe producer 边界。
- `user-shell`: 扩展交互式 shell 需求，要求 prompt、键盘输入、命令执行和 stdout/stderr 输出在默认文本控制台上可见且有界。
- `runtime-smoke-validation`: 扩展验证需求，要求 Stage 20 的交互控制台改动保留 headless 串口/日志行为断言，并记录无法自动化验证的人工控制台风险。

## Impact

- Affected subsystems: kernel terminal/TTY/console path, keyboard input handoff, syscall fd/VFS I/O surface, userland `/bin/sh`, PID-1 init handoff, and runtime validation tooling/notes.
- Architecture assumptions: current runnable backend remains x86_64 with the existing Legacy BIOS/MBR/exFAT path; no UEFI, SMP, or second architecture backend is introduced.
- Memory/layout assumptions: no change to kernel link addresses, page-table layout, direct-map assumptions, syscall vector, CR3 switching contract, or user/kernel ABI.
- Emulator/toolchain assumptions: validation prefers the existing cross toolchain, xmake build flow, QEMU headless serial/log checks, and optional graphical/manual QEMU or Bochs console checks when local keyboard/display support is available.
- Disk/storage assumptions: boot packaging and bounded userland binaries continue to use the existing disk image and storage path; no new persistent full writable filesystem or broad device support is required.
