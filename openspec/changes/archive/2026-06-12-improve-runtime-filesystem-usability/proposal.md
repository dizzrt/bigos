## Why

Stage 24 需要把当前 RAM-backed `/rw` 从“可写 smoke 能力”提升为简单静态 C 程序可依赖的有界运行时文件系统行为。现在应收敛文件创建、读写、seek、sync、目录变更、目录枚举、删除和错误报告的用户可见契约，让后续用户态程序、shell 重定向和行为验证有稳定边界。

## What Changes

- 明确 `/rw` 运行时文件行为是最小可用系统目标的一部分，覆盖简单 C 程序可通过 syscall/libc 使用的文件创建、打开、读取、写入、定位、同步、目录创建、最小目录枚举和删除。
- 扩展现有可写文件系统规格，补齐运行时可用性要求：跨 fd/进程可见性、打开文件引用下的删除语义、目录操作边界、容量/路径/权限失败的确定性结果，以及 `fsync`/缓存写回的可观察行为。
- 扩展 fd/VFS 与 syscall 规格，要求运行时文件操作在用户态 fd、`fork`/`execve` 继承、dup/redirection、阻塞上下文守卫和用户缓冲校验下组合稳定。
- 扩展最小用户态 libc 规格，要求公开并文档化简单 C 程序可使用的文件 I/O wrapper、常量与 errno 翻译，不引入 hosted `FILE` 文件流。
- 增加面向行为的验证要求，覆盖简单 C 程序和 shell 可观察路径中的创建、写入、读回、seek、fsync、mkdir、最小目录枚举、unlink、错误码和边界失败。
- 非目标：不提供跨重启持久化的完整可写文件系统、不引入 broad file-backed `mmap`、async I/O、journaling、rename、硬/软链接、ACL/xattr、完整 POSIX 权限模型、完整 POSIX `readdir/getdents` 兼容、SMP 或新的存储/设备 backend。

## Capabilities

### New Capabilities

- 无。本 change 收敛并扩展既有文件系统、VFS/syscall 与用户态 libc 能力，不新增独立 capability。

### Modified Capabilities

- `writable-filesystem`: 将 RAM-backed 可写后端的要求提升为有界运行时文件系统可用性契约，覆盖简单 C 程序可依赖的文件、目录枚举、删除、sync、错误和容量边界。
- `fd-vfs-shell`: 明确运行时文件操作通过 fd/VFS 与进程 fd 表组合时的引用、继承、dup/redirection、阻塞上下文和只读/可写后端边界。
- `syscall-entry`: 明确用户态文件相关 syscall 的组合语义、用户缓冲校验、append-only ABI 约束和确定性 errno 返回。
- `user-libc-min`: 明确最小 libc 暴露的文件 I/O wrapper、头文件常量和 errno 翻译边界，支撑简单 C 程序使用运行时文件系统。
- `posix-like-process-io-subset`: 将运行时文件创建、读写、seek、sync、目录和删除纳入有界 POSIX-like I/O 子集，同时保持非完整 POSIX 边界。
- `runtime-smoke-validation`: 扩展行为验证目标，覆盖运行时文件系统的用户可见组合路径与环境依赖记录。

## Impact

- Affected subsystems: `kernel/core/fs`、`kernel/core/proc`、`kernel/core/syscall`、`user`、`include`、`docs`、`tests` 与 OpenSpec 规格。
- Boot/backend assumptions: 当前可运行 backend 仍为 x86_64 Legacy BIOS/MBR/exFAT；本 change 不改变 bootloader、MBR/分区、exFAT 只读启动资产、linker 地址、IDT/syscall vector、CR3 切换或 page-table layout。
- Memory assumptions: 可写运行时存储仍为 RAM-backed、有界容量；缓存和元数据分配失败必须确定性返回错误或不发布挂载，不得依赖动态无限增长。
- Emulator/disk assumptions: QEMU/Bochs 可用于环境具备时的运行时 smoke；磁盘镜像仍提供只读启动资产，运行时可写状态不承诺跨重启持久化。
- Toolchain assumptions: 继续以 xmake 和 `x86_64-elf-gcc`/`x86_64-elf-g++` 为目标构建；Python 辅助验证通过 `uv run ...` 执行。
- Documentation impact: 如更新 `docs/en`，必须同步 `docs/zh` 同路径镜像；roadmap 只保留规划级描述，不写入实现入口、命令或 marker 细节。
