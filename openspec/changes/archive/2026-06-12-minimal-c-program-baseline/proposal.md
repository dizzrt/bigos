## Why

此前阶段已经让 BigOS 具备默认 resident init、`/bin/sh` 和可见交互控制台，但简单静态 C 用户程序仍主要是运行时能力的消费者，而不是明确的一等兼容目标。简单 C 程序基线需要把进程入口、参数/环境传递、syscall wrapper、错误报告、基础输出和小型打包工具收敛为稳定、可验证、可文档化的最小 C 程序运行基线。

## What Changes

- 将简单静态链接 C 用户程序定义为简单 C 程序基线的用户可见兼容目标，要求其能通过现有 crt0/libc/syscall surface 可靠启动、取参、输出、报告错误并退出。
- 收敛 `argc`/`argv`/`envp` handoff、`main` 返回路径、`errno` 翻译和 libc wrapper 返回约定，使用户程序不需要了解内核内部 syscall 负 errno 细节。
- 扩展小型 `/bin/*` 打包程序集合，使其覆盖参数解析、stdout/stderr、文件描述符 I/O、退出码和错误路径等基础行为。
- 要求构建与打包路径把这些 C 程序作为稳定产物处理，并保持体积、链接模型和磁盘路径有界。
- 增强行为导向验证，优先通过 runtime-observable 输出、退出码和文件/管道交互断言来保护简单 C 程序基线。
- 明确 non-goals：不引入动态链接、共享库、hosted runtime、完整 POSIX libc、完整 shell 语义、作业控制、SMP、新 boot backend 或新架构运行时等价能力。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `user-crt0-runtime`: 将 crt0 的入口栈消费、`main(argc, argv, envp)` 调用和 `main` 返回后退出路径提升为简单 C 程序兼容契约。
- `user-libc-min`: 稳定 syscall wrapper、`errno` 翻译、基础输出和最小 helper 行为，使普通 C 程序能使用一致的错误报告与 I/O 约定。
- `user-program-build`: 将多个小型静态 C 用户程序作为一等构建/打包目标，要求产物路径、链接方式、体积边界和失败语义稳定。
- `user-shell`: 要求 shell 能执行并展示 基线小型 C 程序的参数、输出、退出和错误行为，但不扩展为完整 POSIX shell。
- `runtime-smoke-validation`: 增加面向简单 C 程序的行为断言，覆盖入口参数、环境、wrapper/errno、基础输出、退出码和小型程序组合。

## Impact

- Affected subsystems: `user` freestanding crt0/libc、小型 `/bin/*` 程序、用户程序构建/镜像打包路径、`/bin/sh` 执行路径、syscall wrapper 使用面和 runtime validation。
- Architecture assumptions: 当前可运行 backend 仍为 x86_64 Legacy BIOS/MBR/exFAT；不引入 UEFI、SMP 或第二架构 runtime parity。
- Memory/layout assumptions: 不改变 kernel link address、user/kernel 地址空间约定、页表布局、direct map、CR3 切换、syscall vector、`int 0x80` ABI 或现有 ELF64 装载边界。
- Emulator/toolchain assumptions: 构建继续依赖现有 xmake 与 `x86_64-elf-*` 交叉工具链；自动化验证优先使用 QEMU headless serial/log 行为观察，图形 QEMU/Bochs 仅作为可用时的补充。
- Disk/storage assumptions: 继续沿用现有 boot/storage path 和镜像打包流程，仅新增或稳定有界 `/bin/*` 用户程序；不引入持久完整可写文件系统或广泛设备支持。
