## Why

当前 BigOS 已具备默认交互式 `/bin/sh`、路径工具、pipe、dup、重定向、cwd 和有界文件 I/O 基线，但 shell 的组合使用体验还需要收紧为可依赖的用户可见契约。现在应在不扩大 POSIX 承诺的前提下，硬化路径处理、错误报告、退出状态传播、pipe/重定向恢复和小工具组合，让最小可用系统的交互路径更稳定。

## What Changes

- 硬化 shell 解析与执行错误：对不支持语法、路径过长、命令缺失、重定向失败、pipe 建立失败和 exec 失败输出确定性错误，并保持 shell 循环可恢复。
- 稳定退出状态传播：shell MUST 保留最近一次内建、外部命令、pipe 或重定向组合的有界退出状态，供验证路径和后续 shell 行为观察。
- 强化路径与 fd 状态隔离：重定向或 pipe 设置失败时，不破坏父 shell 的 stdin/stdout/stderr 和无关 fd。
- 扩展小工具组合契约：路径工具、简单 C 程序和内建命令在 pipe、重定向、相对路径和 PATH 查找下保持可观察、可恢复。
- 增加行为导向验证要求：覆盖成功组合、失败组合、退出状态、错误输出和环境不可用时的显式跳过记录。

## Capabilities

### New Capabilities

- 无：本 change 不新增独立 capability，而是在现有 shell、进程/I/O、路径工具和运行时验证能力上收紧用户可见行为。

### Modified Capabilities

- `user-shell`: 收紧 shell 错误恢复、退出状态传播、pipe/重定向组合、PATH/相对路径消费和父 shell fd 隔离要求。
- `posix-like-process-io-subset`: 将 shell 可用性硬化纳入当前有界 POSIX-like 进程与 I/O 子集，明确组合行为和非目标边界。
- `userland-path-tools`: 要求路径工具作为 shell 组合消费者时保持 deterministic errno/exit-status 行为，并不扩大为完整 POSIX 工具集。
- `runtime-smoke-validation`: 增加 shell usability 行为验证覆盖和环境依赖跳过记录要求。

## Impact

- Affected subsystem: userland shell、用户 libc wrapper、packaged `/bin/*` 小工具、fd/VFS、pipe/dup、process wait/exit 与运行时验证路径。
- Architecture assumptions: 当前可运行 backend 仍为 x86_64 Legacy BIOS/MBR/exFAT；不新增 UEFI、第二 ISA 或 architecture runtime parity。
- Memory/layout assumptions: 不改变 boot/linker 地址、IDT/syscall vector、page-table layout、CR3 切换或磁盘布局；所有新增行为必须保持有界缓冲和 freestanding 用户态约束。
- Emulator/toolchain assumptions: 运行时验证依赖 `x86_64-elf-*`、xmake、QEMU/Bochs、raw image、serial/display/ROM 配置；不可用时必须记录 skipped validation、替代检查和剩余风险。
- Non-goals: 不实现 job control、background jobs、terminal process groups、sessions、termios、完整 POSIX shell grammar、globbing、quoting expansion、variables、scripting、动态链接、完整 POSIX libc、持久完整可写文件系统、async I/O、SMP 或 broad file-backed `mmap`。
