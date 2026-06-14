# BigOS Roadmap / BigOS 路线图

Language: English | 简体中文

This roadmap is the planning entry point for BigOS after the current bounded
userland baseline. It summarizes completed capabilities at a high level and
leaves clear space for future work. It does not replace detailed architecture,
implementation, validation, or change-tracking documents.

本文档是 BigOS 在当前有界用户态基线之后的规划入口。它只在高层概述已完成能力，
并为后续工作留出清晰入口。它不替代详细架构、实现、验证或变更追踪文档。

Project goal: grow BigOS from the current x86_64 research kernel toward a more
general-purpose, multi-architecture kernel with explicitly bounded POSIX-like
compatibility subsets. The current runnable implementation remains x86_64-only
with the existing legacy boot/storage path as the default baseline and a
runnable x86_64 UEFI boot backend spike available as a non-parity backend.

项目目标：将 BigOS 从当前 x86_64 研究内核逐步推进为更通用、支持多架构，并具备明确有界
POSIX-like 兼容子集的内核。当前可运行实现仍是 x86_64-only，现有 legacy boot/storage
路径仍是默认基线，同时已有一个不具备运行时等价能力的 x86_64 UEFI boot backend spike。

## Current Implementation Summary / 当前实现概述

BigOS currently provides a smoke-tested, single-core, mostly synchronous kernel
with a bounded userland. At a high level, the implemented baseline includes:

BigOS 当前提供一个经过 smoke 验证、单核、以同步为主、具备有界用户态的内核。从高层看，
当前已实现基线包括：

- A working x86_64 boot and kernel runtime foundation with text/serial output,
  interrupt/exception/syscall dispatch, basic timers, keyboard input, and a
  single-core scheduler.
- 可工作的 x86_64 启动与内核运行基础，包括文本/串口输出、中断/异常/syscall 分发、
  基础计时、键盘输入和单核调度。
- Kernel memory management covering physical allocation, slab-style allocation,
  kernel virtual memory, direct mapping, user address-space management, and
  bounded user fault handling.
- 内核内存管理，包括物理页分配、slab 风格分配、内核虚拟内存、direct map、用户地址
  空间管理和有界用户 fault 处理。
- A bounded process and syscall layer with process lifecycle management,
  file-descriptor based I/O, anonymous demand paging, `fork`/COW, signals,
  time/identity primitives, image replacement, and per-process current-directory
  state.
- 有界进程与 syscall 层，包括进程生命周期管理、基于文件描述符的 I/O、匿名 demand
  paging、`fork`/COW、signals、time/identity 原语、进程镜像替换和每进程 current
  directory 状态。
- A minimal storage and filesystem layer with synchronous block I/O, read-only
  boot assets, a bounded writable runtime area, constrained rename, page/buffer
  cache, pipes, fd duplication, relative path resolution, and bounded
  file/directory metadata queries.
- 最小存储与文件系统层，包括同步块 I/O、只读启动资产、有界可写运行时区域、
  受限 rename、page/buffer cache、pipe、fd duplication、相对路径解析和有界文件/目录元数据查询。
- A minimal freestanding userland with resident init behavior, an interactive
  text-console shell, basic libc-style support, current-directory and rename
  wrappers, and small packaged path/user programs.
- 最小 freestanding 用户态，包括常驻 init 行为、交互式文本控制台 shell、基础 libc
  风格支持、current-directory 与 rename wrapper，以及小型路径/用户程序。

## Current Boundary / 当前边界

BigOS is a controlled research kernel, not a complete general-purpose OS. Keep
future planning and documentation within these boundaries until a specific stage
changes them:

BigOS 是受控研究内核，不是完整通用 OS。在新的阶段明确改变前，后续规划和文档都应保持
以下边界：

- Current runnable backends: x86_64 with the existing Legacy BIOS style boot
  flow as the default baseline, plus an x86_64 UEFI boot backend spike; UEFI and
  additional architectures are not yet runtime-parity backends.
- 当前可运行 backend：x86_64 与现有 Legacy BIOS 风格启动流程仍是默认基线，另有
  x86_64 UEFI boot backend spike；UEFI 和其他架构尚不具备运行时等价能力。
- Execution model: single-core, mostly synchronous, bounded userland, no SMP.
- 执行模型：单核、以同步为主、有界用户态、无 SMP。
- Userland: minimal static user programs and a bounded interactive console path,
  no dynamic linking/shared libraries, no job control, no terminal process
  groups, no complete POSIX libc.
- 用户态：最小静态用户程序与有界交互式控制台路径，无动态链接/共享库、无作业控制、
  无终端进程组、无完整 POSIX libc。
- Source organization: kernel implementation and freestanding userland are
  separate top-level domains; future planning should preserve that boundary.
- 源码组织：内核实现与 freestanding 用户态是分离的顶层域；后续规划应保持这一边界。
- Memory/file model: bounded anonymous demand paging/COW and bounded writable
  runtime storage, but no broad file-backed `mmap`, no persistent full writable
  filesystem, no async I/O, and no broad storage/device support.
- 内存/文件模型：bounded anonymous demand paging/COW 与有界可写运行时存储，但无广泛
  file-backed `mmap`、无持久完整可写文件系统、无 async I/O、无广泛存储/设备支持。
- Boot/backends: the UEFI backend is a completed x86_64 boot spike, while
  storage, device, ISA backends, and UEFI runtime parity remain future or
  parallel-track items.
- 启动/backend：UEFI backend 已完成 x86_64 boot spike；storage、device、ISA
  backend 以及 UEFI 运行时等价能力仍是后续或并行轨道事项。

## Completed Capability Baseline / 已完成能力基线

Stages 20 through 38 are complete and now form a compressed minimal usable
system baseline. The completed work can be treated as the foundation for future
planning rather than as individual future-stage items.

阶段 20 到阶段 38 已完成，并共同形成压缩后的最小可用系统基线。后续规划应将这些工作视为
基础能力，而不是继续把它们作为未来阶段逐项展开。

- Kernel foundation: x86_64 legacy boot, text/serial output,
  interrupt/exception/syscall dispatch, timer and keyboard input, single-core
  scheduling, kernel memory management, user address spaces, and bounded user
  fault handling.
- 内核基础：x86_64 legacy boot、文本/串口输出、中断/异常/syscall 分发、计时器与键盘输入、
  单核调度、内存管理、用户地址空间和有界用户 fault 处理。
- User-visible system baseline: resident init, interactive shell, static user
  programs, bounded libc-style support, process lifecycle, `exec`/`fork`/COW,
  wait/exit behavior, signals, time and identity primitives, fd-based I/O,
  pipes, duplication, and redirection.
- 用户可见系统基线：常驻 init、交互式 shell、静态用户程序、有界 libc 风格支持、进程生命周期、
  `exec`/`fork`/COW、wait/exit 行为、signals、time 与 identity 原语、基于 fd 的 I/O、
  pipe、duplication 和重定向。
- Filesystem and path baseline: read-only boot assets, bounded writable runtime
  storage, constrained rename, page/buffer cache, file and directory metadata,
  per-process current directories, relative path resolution, and small packaged
  path tools.
- 文件系统与路径基线：只读启动资产、有界可写运行时存储、受限 rename、page/buffer cache、
  文件与目录 metadata、每进程 current directory、相对路径解析和小型打包路径工具。
- Expansion foundations: the runnable x86_64 UEFI boot backend spike, initial
  x86_64/core decoupling, terminal preparation, and SMP preparation contracts
  for locking, per-CPU state, interrupt routing, TLB invalidation, and memory
  ordering.
- 扩展基础：可运行的 x86_64 UEFI boot backend spike、初步 x86_64/core 解耦、终端能力准备，
  以及围绕 locking、per-CPU state、中断路由、TLB invalidation 和 memory ordering 的 SMP
  准备契约。

## Future Mainline / 后续主线

Future work should move BigOS toward a more mature and usable system while
preserving the current x86_64-only delivery target. Multiple expansion tracks
may progress in parallel, but each mainline stage should still have a clear
user-visible capability goal.

后续工作应推动 BigOS 走向更成熟、可用的系统，同时保持当前 x86_64-only 的交付目标。
多个扩展方向可以并行推进，但每个主线阶段仍应有清晰的用户可见能力目标。

### Stage 39: Usability And POSIX Surface Hardening / 可用性与 POSIX 表层能力硬化

- Mature the existing shell, process, file, fd, pipe, redirection, signal, time,
  identity, and error-reporting behavior into a more coherent bounded Unix-like
  environment.
- 将已有 shell、进程、文件、fd、pipe、重定向、signal、time、identity 和错误展示行为
  打磨为更一致的有界 Unix-like 环境。
- Expand POSIX-like compatibility only as explicit bounded subsets, prioritizing
  observable behavior and predictable error contracts over broad compatibility
  claims.
- 仅以明确有界子集的形式扩展 POSIX-like 兼容能力，优先完善可观察行为和可预测错误契约，
  不提前声明广泛兼容。
- Keep x86_64 development disciplined around architecture boundaries so short-term
  maturity work does not block future ISA backends.
- x86_64 开发必须保持架构边界纪律，避免短期成熟化工作阻碍未来 ISA backend。

### Stage 40: Standard C Library Foundation / 标准 C 库基础

- Turn the current bounded libc-style support into a clearer standard C library
  foundation for static user programs.
- 将当前有界 libc 风格支持整理为更清晰的标准 C 库基础，服务静态用户程序。
- Prioritize common C library surfaces that directly improve program portability,
  shell tools, diagnostics, file I/O, memory routines, string routines, and
  process interaction.
- 优先补齐能直接提升程序可移植性、shell 工具、诊断、文件 I/O、内存例程、字符串例程和进程交互的
  常见 C 库接口。
- Preserve boundaries around dynamic linking, threads, locale, full hosted stdio,
  and complete POSIX libc until later stages intentionally expand them.
- 在后续阶段明确扩展前，继续保持动态链接、线程、locale、完整 hosted stdio 和完整 POSIX libc
  的边界。

### Stage 41: Filesystem Maturity / 文件系统成熟化

- Improve runtime filesystem semantics for ordinary programs, including stronger
  directory behavior, metadata consistency, permissions edges, capacity errors,
  and file-update behavior.
- 面向普通程序改善运行时文件系统语义，包括更强的目录行为、metadata 一致性、权限边界、容量错误
  和文件更新行为。
- Use this stage to prepare for persistent writable storage without prematurely
  promising journaling, cross-reboot durability, broad storage drivers, or broad
  file-backed mappings.
- 通过本阶段为持久可写存储做准备，但不提前承诺 journaling、跨重启持久化、广泛存储驱动或广泛
  file-backed mapping。

### Stage 42: Process, Terminal, And Shell Compatibility / 进程、终端与 Shell 兼容性

- Broaden the bounded process and terminal model where it directly improves
  interactive use and small program composition.
- 在能直接改善交互使用和小程序组合的范围内，扩展有界进程与终端模型。
- Candidate areas include clearer wait behavior, richer signal interaction,
  improved terminal input semantics, shell error handling, utility composition,
  and a more useful command environment.
- 候选方向包括更清晰的 wait 行为、更丰富的 signal 交互、更完善的终端输入语义、shell 错误处理、
  工具组合和更实用的命令环境。
- Keep sessions, terminal process groups, job control, complete terminal control,
  and full POSIX shell behavior as explicit non-goals until selected later.
- 在后续明确选择前，继续把 session、terminal process group、作业控制、完整终端控制和完整
  POSIX shell 行为作为非目标。

### Stage 43: VM And Program Runtime Expansion / VM 与程序运行时扩展

- Expand the program runtime foundation after libc, filesystem, and process
  contracts are stable enough to support more complex user programs.
- 在 libc、文件系统和进程契约足够稳定后，扩展程序运行时基础以支持更复杂的用户程序。
- Candidate areas include richer user address-space layout, more complete memory
  mapping policy, better loader boundaries, and preparation for future dynamic
  linking.
- 候选方向包括更丰富的用户地址空间布局、更完整的 memory mapping 策略、更清晰的 loader 边界，
  以及为未来动态链接做准备。
- Preserve the boundary that broad file-backed `mmap`, shared libraries, and a
  dynamic loader are separate maturity decisions.
- 保持边界：广泛 file-backed `mmap`、共享库和动态加载器属于独立的成熟度决策。

### Stage 44: Persistent Storage Or Dynamic Linking / 持久存储或动态链接

- Choose one large user-visible expansion as the next mainline milestone once the
  preceding maturity work has stabilized enough common contracts.
- 当前置成熟化工作已经稳定足够多公共契约后，选择一个大型用户可见扩展作为下一主线里程碑。
- A persistent writable filesystem should be favored if the project needs better
  practical usability, development workflow, and long-running system behavior.
- 如果项目更需要实际可用性、开发工作流和长时间运行系统行为，应优先选择持久可写文件系统。
- Dynamic linking should be favored if libc, loader, VM, ABI, and filesystem
  contracts are stable enough to support a reusable user-program ecosystem.
- 如果 libc、loader、VM、ABI 和文件系统契约已经足够稳定，可以支撑可复用用户程序生态，则优先选择
  动态链接。

### Parallel Foundations / 并行基础方向

- SMP may continue as a parallel foundation, but real multi-core execution,
  AP startup, APIC-backed default interrupt delivery, cross-CPU scheduling, and
  user-visible ABI changes remain separate milestones.
- SMP 可以作为并行基础继续推进，但真正多核执行、AP 启动、APIC-backed 默认中断投递、跨 CPU
  调度和用户可见 ABI 变化仍属于独立里程碑。
- Backend work may continue for UEFI runtime parity and future backend cleanup,
  but the short-term plan does not add a new ISA.
- backend 工作可以继续推进 UEFI 运行时等价和后续 backend 清理，但短期计划不接入新 ISA。
- All x86_64 work should avoid spreading architecture-specific assumptions into
  process, filesystem, userland ABI, and generic kernel policy.
- 所有 x86_64 工作都应避免把 architecture-specific 假设扩散到进程、文件系统、用户态 ABI 和通用
  内核策略中。
