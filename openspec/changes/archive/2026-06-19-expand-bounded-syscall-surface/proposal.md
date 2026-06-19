## Why

BigOS 已有 bounded userland、进程生命周期、fd/VFS、信号、进程组/session 与默认终端前台绑定，但用户态 syscall/libc 面仍偏窄，许多可移植小程序需要的等待、文件描述符控制、文件状态查询和基础进程信息只能依赖 BigOS 专用 wrapper 或缺失能力。现在补齐一组有界 primitive，可以让 shell 与小型静态程序更接近 POSIX-like 消费方式，同时继续保持 freestanding-safe 与明确边界。

## What Changes

- 扩展 wait 族接口，提供有界 `wait`/`waitpid`/`WNOHANG` 形态、确定性 status 编码和明确 unsupported options 结果，不实现完整 stopped/continued/job-control 语义。
- 增加有界 fd 控制 primitive，覆盖 close-on-exec 设置/查询、`F_DUPFD`、`dup`/`dup2` 行为补强和有界 `fcntl`-like 能力，使 `execve` 继承边界与 fd duplication 可由用户态显式表达。
- 增加 bounded file/path primitive，包括 `access`/`stat`/`fstat` 消费面、`truncate`/`ftruncate` 统一语义、目录/文件删除错误边界和确定性 metadata 映射。
- 增加基础进程 primitive，包括 parent/process identity 查询、有限 `errno`/libc wrapper、进程组/session 相关查询的一致消费方式，以及失败路径的稳定返回值。
- 更新 syscall ABI 文档、user libc headers/wrappers、shell 或小型用户程序的消费点，并增加 source-level 与可用时的 QEMU headless 行为验证。
- 保持 `int 0x80` 入口机制、IDT vector、CR3 切换、boot handoff、磁盘布局、地址空间布局和已有 default-on init/shell 路径不变。

## Capabilities

### New Capabilities

- `bounded-syscall-surface`: 定义 BigOS 面向小型静态用户程序的有界 syscall/libc 扩展面，包括 wait 变体、fd 控制、文件/路径状态查询、基础进程 primitive、错误码与验证边界。

### Modified Capabilities

- `syscall-entry`: 扩展 syscall 编号、参数/返回约定和用户指针验证要求，同时保持 `int 0x80` ABI 入口不变。
- `process-lifecycle`: 明确 wait 变体、父子关系查询、`execve` close-on-exec 处理和进程退出状态对新增 primitive 的生命周期规则。
- `fd-vfs-shell`: 将新增 fd 控制、metadata 查询、access/truncate 类路径操作纳入 VFS/fd table 的有界语义。
- `posix-like-process-io-subset`: 扩大 bounded POSIX-like 消费面，并继续排除完整 POSIX、完整 job control、动态链接和完整 libc。
- `user-libc-min`: 为新增 syscall 面提供 freestanding-safe headers/wrappers、`errno` 映射和小程序可消费的最小声明。

## Impact

- 影响子系统：`kernel/core/syscall`、`kernel/core/proc`、`kernel/core/fs`、`include/bigos`、`user/libc`、`user/bin`、`user/sh`、`docs/en`、`docs/zh` 与相关 source-level tests。
- 影响 API/ABI：新增或补强有限 syscall 编号、libc wrapper、用户可见结构体/常量和 errno 映射；不改变 syscall vector、寄存器 ABI、boot ABI 或既有 syscall 的成功语义。
- 架构与运行假设：当前交付目标仍为单核 x86_64 Legacy BIOS/MBR/exFAT/bigfs 默认路径；UEFI runtime parity、多 ISA、SMP、async I/O、动态链接和 broad storage/device 支持不属于本 change。
- 内存与布局假设：不改变 kernel link address、用户/内核地址空间布局、page-table self mapping、direct map、用户栈布局或磁盘镜像布局；新增用户结构体必须使用有界 copy-in/copy-out。
- 工具链与验证假设：实现阶段需要 x86_64-elf cross toolchain、xmake、`uv run` 辅助检查，以及可用时的 QEMU headless smoke；若工具链、模拟器或磁盘镜像配置不可用，validation notes 必须记录缺失条件、替代检查和剩余风险。
