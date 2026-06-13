## Why

当前 BigOS 的路径能力主要以绝对路径为边界，简单程序和 shell 在处理运行时文件时仍需要显式拼接完整路径。现在最小用户态、fd/VFS、元数据和 shell baseline 已经形成，可以引入每进程 current directory 与一致的相对路径解析，让后续路径工具和 shell 可用性阶段有稳定基础。

## What Changes

- 为每个进程引入有界 current-directory contract，支持查询与切换当前目录，并定义进程创建、`fork`、`execve`、退出和回收中的继承与生命周期规则。
- 扩展接收路径的内核接口，使 `open`、目录操作、元数据查询、`execve` 等路径入口按统一规则解析绝对路径与相对路径。
- 路径解析明确支持 BigOS 当前目录树内的 POSIX-style `.`/`..` 组件语义，root 的父目录保持 root。
- 在用户态 libc 暴露有界 `getcwd`、`chdir`、`ERANGE` 和相对路径可用的路径 wrapper，使简单静态 C 程序不需要自行维护全局路径前缀。
- 让 `/bin/sh` 能在自身进程内维护并消费 cwd，至少支持内建 `cd` 与提示/错误报告所需的可观察路径行为，并添加小型 `/bin/pwd` 用户工具。
- 增加行为导向验证，覆盖 cwd 继承、`execve` 保留、相对路径成功/失败、只读与 `/rw` 后端差异、以及 shell/libc 可观察路径。
- 明确非目标：不引入 mount namespace、`chroot`、符号链接遍历、完整 POSIX pathname 解析、持久完整可写文件系统、async I/O、SMP 或新 boot/architecture runtime parity。

## Capabilities

### New Capabilities
- `current-directory-relative-paths`: 定义每进程 current directory、POSIX-style `.`/`..` 组件解析、libc/shell/`/bin/pwd` 暴露和行为验证的有界能力。

### Modified Capabilities
- `fd-vfs-shell`: 路径型 fd/VFS 操作从绝对路径-only 扩展为统一解析绝对路径与相对路径，并保持后端、阻塞上下文和错误边界。
- `process-lifecycle`: 进程对象需要拥有 cwd 状态，并在 `fork`/`execve`/退出/回收中保持有界继承与释放语义。
- `file-metadata-contract`: path metadata query 需要支持相对路径解析，同时保持无符号链接、无 namespace、无完整 canonicalization 边界。
- `user-libc-min`: 最小 libc 需要暴露 cwd/path wrapper 与 errno 行为，且不扩大为完整 POSIX libc。
- `user-shell`: shell 需要消费 cwd 能力，提供有界 `cd` 行为并让相对命令/重定向路径按统一规则工作。
- `posix-like-process-io-subset`: 有界 POSIX-like 子集需要把 cwd 与相对路径纳入用户可见进程/I/O 行为边界。

## Impact

- Affected boot/kernel subsystem: normal x86_64 Legacy BIOS runtime path after kernel init, process/syscall/fs/userland subsystems; no bootloader, linker address, interrupt vector, disk layout, CR3 switching, or storage-driver contract changes are intended.
- Affected kernel areas: `kernel/core/proc` process state and lifecycle, `kernel/core/fs` path lookup and backend dispatch, `kernel/core/syscall` path-taking syscall validation, and public headers under `include/bigos`.
- Affected userland areas: `user` libc wrappers/headers, `/bin/sh`, small packaged user programs or validation consumers.
- Validation assumptions: source/build checks remain local; runtime behavior checks depend on configured `x86_64-elf-*` toolchain, QEMU/Bochs availability, current raw disk image path, and existing emulator display/ROM setup. Missing environment-dependent checks must be recorded as skipped rather than claimed as passed.
- Architecture and memory assumptions: current runnable backend remains single-core x86_64 with the existing virtual memory layout and bounded user-buffer validation. Cwd strings and path buffers must remain bounded and freestanding-safe.
