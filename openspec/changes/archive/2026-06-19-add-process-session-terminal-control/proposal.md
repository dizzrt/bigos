## Why

BigOS 当前已有进程生命周期、信号、默认终端抽象和交互式 shell，但仍缺少进程组、session 与前台终端归属这一层用户可见控制模型。补齐该能力后，shell 可以表达有界的 foreground command 语义，并为后续更成熟的作业控制类交互打下清晰边界。

## What Changes

- 引入有界 process group 与 session 状态：进程具备 pid/pgid/sid 关系，`fork` 继承控制归属，`execve` 保持归属，进程退出与 reap 会清理相关引用。
- 在默认控制台终端抽象上增加前台进程组绑定：默认终端持有一个 bounded foreground pgid，并通过普通 syscall/fd/terminal 路径被 shell 和用户程序观察。
- 扩展有界 syscall 与 libc wrapper，使简单静态程序和 shell 可以创建/查询/调整 process group、session 与前台终端绑定，并获得确定性 errno。
- 将 interrupt-like terminal input、foreground command wait 和信号投递对齐到有界前台进程组模型，但不实现完整 POSIX job control、后台作业、`termios` 或多终端。
- 更新 shell 的 foreground command 消费模型：外部命令和单级 pipe 可以在有界前台进程组中运行，shell 在命令完成后恢复自身前台绑定。
- 增加行为验证与文档同步，证明默认 x86_64 Legacy BIOS 路径下的前台终端绑定、进程组/session 查询、错误恢复和边界说明可复现。

## Capabilities

### New Capabilities

- `process-session-terminal-control`: 定义 BigOS 有界 process group、session 与默认终端前台绑定能力，包括继承、查询、变更、终端归属、前台输入/信号语义和失败边界。

### Modified Capabilities

- `posix-like-process-io-subset`: 将 process group、session、foreground terminal binding 纳入有界 POSIX-like 子集，同时继续排除完整 POSIX job control、后台作业、`termios`、多终端和完整 shell 语义。
- `process-lifecycle`: 明确进程创建、镜像替换、退出和 reap 对 pgid/sid/foreground 归属的生命周期规则。
- `minimal-terminal-abstraction`: 将默认控制台终端从“无 terminal process group”推进到“单默认终端的有界 foreground pgid 绑定”，并保留单终端、非 `termios` 边界。
- `user-shell`: 使 shell 消费有界前台进程组模型运行 foreground command、单级 pipe 和恢复 shell 前台绑定，而不声明完整 job control。
- `signals`: 扩展 terminal interrupt-like input 的信号目标边界，使其可面向当前 foreground process group 产生有界结果。

## Impact

- 影响子系统：`kernel/core/proc`、`kernel/core/syscall`、`kernel/core/terminal`、`kernel/core/signal`、`user/libc`、`user/bin/sh` 与相关用户态验证程序。
- 影响 API/ABI：新增或扩展有限 syscall 编号、用户可见 libc wrapper、进程状态字段和终端控制查询/设置接口；不改变 `int 0x80` 入口机制、IDT vector、CR3 切换规则或现有 boot handoff ABI。
- 架构与运行假设：当前交付目标仍为单核 x86_64 Legacy BIOS/MBR/exFAT 默认路径；UEFI runtime parity、多 ISA、SMP、async I/O 和 broad storage/device 支持不属于本 change。
- 内存与布局假设：不改变 kernel link address、用户/内核地址空间布局、page-table self mapping、direct map 或磁盘布局；新增状态使用 freestanding-safe、可界定生命周期的内核存储。
- 工具链与验证假设：实现阶段需要 x86_64-elf cross toolchain、xmake，以及可用时的 QEMU headless smoke；若工具链、模拟器或磁盘镜像配置不可用，验证记录必须说明缺失条件、替代检查和剩余风险。
