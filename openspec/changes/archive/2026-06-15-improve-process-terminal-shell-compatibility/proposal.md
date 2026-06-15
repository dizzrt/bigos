## Why

Stage 42 需要在 Stage 39-41 的有界用户态基线之上，继续改善交互使用和小程序组合体验。当前 BigOS 已有进程生命周期、wait、signals、默认控制台终端、`/bin/sh`、pipe、fd 重定向和路径工具，但这些能力在组合场景下还需要更清晰的用户可见契约，避免交互路径可用但失败行为、控制输入和状态传播不稳定。

## What Changes

- 收紧进程组合行为：明确 `wait`/`waitpid` 的有界匹配、非阻塞/unsupported options 错误、信号终止状态和 shell 可观察状态传播。
- 扩展默认终端控制输入：把换行、退格、EOF-like 和 interrupt-like 输入整理为非 IRQ consumer 路径可观察的有界行为。
- 硬化 shell 交互体验：改进错误报告、退出状态保存、pipe/重定向失败恢复、命令组合和 prompt/输入恢复行为。
- 扩展小程序组合契约：要求简单静态用户程序、路径工具、pipe、重定向、cwd/PATH 和 shell 错误路径保持 deterministic 行为。
- 增加行为导向验证：覆盖进程 wait/signal 状态、终端控制输入、shell 失败恢复、pipe/重定向组合，以及环境不可用时的显式跳过记录。
- 不改变 syscall 入口 ABI、`int 0x80` 向量、x86_64 Legacy BIOS 默认启动路径、磁盘布局、用户 ELF 静态链接边界或现有有界 VFS/storage 边界。

## Capabilities

### New Capabilities

- 无。该 change 面向 Stage 42 的兼容性整理，修改现有进程、终端、shell 和 bounded POSIX-like 子集能力，不新增独立顶层能力域。

### Modified Capabilities

- `process-lifecycle`: 收紧 wait/waitpid 的有界匹配、状态编码、unsupported options 和失败恢复要求。
- `signals`: 明确信号终止状态与等待/父进程观察之间的组合契约。
- `minimal-terminal-abstraction`: 扩展默认控制台终端的控制输入、EOF-like/interrupt-like 事件和非 IRQ 消费边界。
- `user-shell`: 硬化交互读行、错误恢复、退出状态保存、pipe/重定向组合和终端控制输入消费行为。
- `posix-like-process-io-subset`: 将 Stage 42 的进程、终端、shell 组合语义纳入有界 POSIX-like 进程与 I/O 子集，同时继续声明非目标。

## Impact

- 受影响子系统：`kernel/core/proc/**`、`kernel/core/signal/**`、`kernel/core/terminal/**`、`kernel/core/fs/**`、`kernel/core/syscall/**`、`user/libc/**`、`user/sh/**`、`user/bin/**` 和行为验证记录。
- 受影响 API：现有 `wait`/`waitpid` wrapper、signal status 观察、stdin/stdout/stderr 终端路径、shell 内建/外部命令执行、pipe、dup/redirection 和 errno/exit-status 表层；原则上不新增 syscall 编号。
- 架构假设：当前交付目标仍是 x86_64 Legacy BIOS/MBR/exFAT；不要求 UEFI runtime parity、第二 ISA backend、SMP、APIC 默认中断投递或跨 CPU 调度。
- 内存与 ABI 假设：不改变 kernel link address、page-table layout、direct map、GDT/TSS、CR3 switching、InterruptFrame、syscall vector `0x80`、user/kernel ABI、disk layout 或 boot handoff ABI。
- 运行时边界：用户态继续是静态 freestanding ELF；不引入完整 POSIX shell、sessions、terminal process groups、job control、termios、伪终端、动态链接、完整 POSIX libc、async I/O、持久完整 writable filesystem 或 broad file-backed `mmap`。
- 验证假设：构建依赖 xmake 与 `x86_64-elf-*` 交叉工具链；运行时验证优先使用现有 QEMU/Bochs Legacy BIOS 路径，环境不可用时必须记录 skipped validation、替代检查和剩余风险。
