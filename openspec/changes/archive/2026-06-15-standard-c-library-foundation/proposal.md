## Why

BigOS 已经具备可运行的有界用户态、静态用户程序、`crt0`、最小 libc、shell 与小型 `/bin/*` 工具，但当前 C 库表面仍以逐步补丁式能力为主，缺少面向 Stage 40 的清晰“标准 C 库基础”边界。现在需要把这些已有能力整理为可依赖、可验证、可继续扩展的 bounded C library subset，服务简单静态 C 程序、shell 工具、诊断输出、文件 I/O、内存/字符串例程和进程交互。

## What Changes

- 将用户态 libc 从“最小可用 wrapper 集合”提升为“标准 C 库基础子集”，明确 public headers、errno、syscall wrapper、字符串/内存函数、stdlib、最小 stdio、环境访问、文件/进程 wrapper 和 BigOS-specific helper 的边界。
- 补齐或整理高价值 C 库接口，优先服务静态用户程序和 packaged tools 的可移植性，而不是追求完整 hosted libc。
- 固化 `crt0`、libc wrapper 与内核用户态 ABI 的分层：用户程序通过 freestanding public headers 和 libc wrapper 消费能力，raw syscall primitive 继续作为受限低层 helper。
- 增强 libc 行为验证，覆盖头文件一致性、errno 翻译、字符串/内存例程、分配失败语义、stdio/error reporting、文件/进程 wrapper 和代表性用户程序行为。
- 保持明确非目标：不引入动态链接、共享库、动态 loader、完整 POSIX libc、线程、locale、完整 `FILE` 流、完整 terminal/job-control/session/process-group、broad file-backed `mmap`、持久完整可写文件系统、async I/O、SMP 或广泛 POSIX 兼容声明。

## Capabilities

### New Capabilities

- 无。Stage 40 建立在现有用户态 libc、crt0 和有界 POSIX-like 进程/I/O 子集之上，本 change 通过修改既有能力来收敛契约。

### Modified Capabilities

- `user-libc-min`: 将最小 libc 规格扩展为 Stage 40 标准 C 库基础子集，明确 public header、wrapper、stdlib/string/memory/stdio/error-reporting/validation 的可观察要求与非目标。
- `user-crt0-runtime`: 明确 `crt0` 仍服务静态 C 程序入口，不引入动态 loader、共享库或 hosted runtime，并与 libc 基础子集保持启动/退出契约一致。
- `posix-like-process-io-subset`: 明确 Stage 40 的 C 库扩展仍只是有界 POSIX-like 进程/I/O 子集的用户态消费层，不扩大为完整 POSIX 兼容。

## Impact

- Affected subsystems: `user` 顶层用户态运行时与 libc，`user/bin` packaged tools，`user/smoke` 用户态验证程序，`include/bigos` 中与用户 ABI 共享的 errno/syscall 常量，以及相关 docs/OpenSpec 规格。
- Boot/kernel assumptions: 当前 runnable 目标仍是 x86_64 Legacy BIOS/MBR/exFAT 默认路径；本 change 不改变 boot handoff、ELF 装载地址、页表布局、interrupt/syscall vector、磁盘布局、CR3 切换或内核 ABI。
- Memory assumptions: 继续使用现有有界用户地址空间、`brk`、restricted anonymous mapping 和 demand paging/COW 能力；不要求 broad file-backed `mmap`、共享库页共享或动态 loader 映射策略。
- Toolchain/emulator assumptions: 用户程序继续以 freestanding、`-nostdlib`、静态链接方式构建，依赖 x86_64 cross toolchain；运行时验证仍以当前 QEMU/Bochs 可用性为环境条件，缺失时记录跳过原因和残余风险。
- Compatibility impact: 不声明广泛 POSIX 兼容；所有新增或整理的 C 库接口必须有实现、规格和文档边界，未实现接口不得通过头文件或文档暗示为可用。
