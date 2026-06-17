## Why

TTY console input capability2 将 BigOS 现有 freestanding 用户态支持推进为有文档边界的最小 C 标准库子集，使简单静态 C 程序可以依赖稳定、可验证、与实际 syscall 行为一致的 libc 接口。

当前用户态 libc 已覆盖若干基础能力，但其接口边界、错误语义、stdio 限制、堆行为和头文件暴露范围仍需要收敛为明确契约，以便后续 POSIX-like 进程/I/O、运行时文件系统和行为验证阶段可以在同一用户态兼容基线上推进。

## What Changes

- 将 `user-libc-min` 从“早期最小 helper 集合”整理为文档化的最小 C 标准库子集。
- 明确 libc wrapper、errno、字符串/内存函数、最小 stdio、`fprintf(stderr, ...)`、环境变量读取、堆分配和细粒度标准头文件名的行为边界。
- 要求最小 libc 与当前内核 syscall、fd/VFS、`brk`、`execve` 参数/环境传递和控制台输出路径保持一致。
- 为简单静态 C 程序定义可依赖的成功/失败语义和错误报告方式。
- 保持非目标：不引入 locale、线程、完整 hosted `stdio`、完整 `FILE` 流、动态加载器、共享库、完整 POSIX libc 或广泛标准兼容声明。
- 保持当前运行假设：x86_64 Legacy BIOS/MBR/exFAT 路径、单核、有界用户态、静态链接程序和现有用户 ELF 装载模型。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `user-libc-min`: 将现有最小用户态 libc 能力扩展为明确的最小 C 标准库子集契约，覆盖接口集合、错误语义、stdio/standard streams 限制、堆行为、环境读取、细粒度头文件边界和专门 libc subset smoke 验证要求。

## Impact

- Affected subsystem: freestanding userland libc and simple C user programs.
- Affected code areas: `user` 中的 crt0/libc、用户程序、用户态头文件；必要时涉及 syscall wrapper 与内核错误码头文件的包含关系。
- Affected behavior: 简单 C 程序通过 libc 调用 syscall、输出文本、使用 `fprintf(stderr, ...)` 报告错误、读取参数/环境和进行有界堆分配的可观察行为。
- Dependencies: 依赖现有 x86_64 `int 0x80` syscall ABI、`execve` 初始栈契约、fd 0/1/2 I/O、`brk`/受限匿名映射、统一 errno 和静态用户程序构建路径。
- Validation scope: 优先通过源级检查、构建检查和专门 libc subset smoke 的运行时可观察行为断言覆盖；依赖模拟器或硬件的检查保持分层，不作为每次变更的强制条件。
