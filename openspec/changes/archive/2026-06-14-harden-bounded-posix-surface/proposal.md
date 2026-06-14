## Why

Stage 39 要把 BigOS 已有 shell、进程、文件、fd、pipe、重定向、signal、time、identity 和错误展示行为打磨成更一致的有界 Unix-like 环境。现有实现已经覆盖大多数内核 syscall 和 libc wrapper，但 signal 表层、wait 命名/状态语义、time POSIX-like wrapper、错误文本展示和 shell/fd 组合行为仍需要规格化，避免用户态接口“能用但契约不清”。

## What Changes

- 补齐有界 signal 用户态表层：公开 `signal.h` 所需的最小 signal 常量、类型、`sigaction`、`sigprocmask`，并定义用户 handler 返回到内核的 bounded `sigreturn` 路径。
- 整理进程等待表层：保留现有 bounded wait 能力，同时提供更清晰的 `wait`/`waitpid` 用户态契约、状态写回语义和 unsupported options 错误行为。
- 增加 POSIX-like 时间与错误展示表层：在现有 `SYS_GET_TIME`、`errno` 基础上提供 bounded `time`、`strerror`、`perror` 或等价 libc 接口。
- 硬化 shell、fd、pipe、redirection 行为：明确 fd 隔离、失败恢复、单级 pipe EOF/close、重定向错误、命令查找失败和外部命令退出状态。
- 强化验证：扩展现有 userland/signal/pipe/time_identity smoke 或源级检查，覆盖新增 wrapper、错误文本和 shell 组合行为。
- 不改变 syscall 入口 ABI、`int 0x80` 向量、x86_64 Legacy BIOS 默认启动路径、磁盘布局、用户 ELF 静态链接边界或现有 bounded VFS/storage 边界。

## Capabilities

### New Capabilities

- 无。该 change 收敛和硬化 Stage 39 已覆盖的现有 bounded userland/POSIX-like 表层，不引入新的顶层能力域。

### Modified Capabilities

- `posix-like-process-io-subset`: 明确 Stage 39 的 bounded POSIX-like 接口清单、wait/signal/time/error/shell 行为契约和非目标。
- `user-libc-min`: 增加或规范 signal、waitpid/time/error 文本相关 libc 表层声明与 wrapper 语义。
- `signals`: 将内核已有 signal syscall 连接到用户态可用的 bounded signal handler/trampoline/sigreturn 契约。
- `user-shell`: 硬化单级 pipe、基础重定向、PATH 查找、错误展示、退出状态和失败后 fd 恢复行为。
- `runtime-smoke-validation`: 增加 Stage 39 表层硬化对应的可观察 smoke/source-contract 验证要求。

## Impact

- 受影响代码区域：`user/libc/**`、`user/sh/**`、`user/bin/**`、`kernel/core/syscall/**`、`kernel/core/signal/**`、`kernel/core/proc/**`、`include/bigos/syscall.h` 以及相关测试/验证脚本。
- 受影响 API：用户态 `signal.h`、`unistd.h`、`sys/wait.h`、`time.h`、`string.h`/`stdio.h` 的最小 bounded 声明；内核 syscall 编号原则上不新增，仅复用现有 `SYS_SIGACTION`、`SYS_SIGPROCMASK`、`SYS_SIGRETURN`、`SYS_GET_TIME`、`SYS_WAIT`。
- 架构假设：当前交付目标仍是 x86_64；必须保持 syscall entry、InterruptFrame、user CR3 切换和 signal frame ABI 的 x86_64 边界清晰，不把架构细节扩散到通用 libc/shell 契约。
- 内存与运行时假设：用户态仍是静态 freestanding ELF；不引入动态链接、线程、locale、完整 hosted stdio、完整 POSIX libc 或 broad file-backed `mmap`。
- 存储与模拟器假设：默认验证仍基于 Legacy BIOS/MBR/exFAT 镜像、RAM-backed `/rw`、QEMU/Bochs 可用时的有界 smoke；不要求 UEFI runtime parity、新 ISA、SMP、async I/O 或持久完整 writable filesystem。
