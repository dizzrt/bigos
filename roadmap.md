# BigOS Roadmap / BigOS 路线图

Language: English | 简体中文

This roadmap is the planning entry point for BigOS after the current bounded
userland baseline. It summarizes completed capabilities at a high level and
leaves clear space for future work. It does not replace detailed architecture,
implementation, validation, or change-tracking documents.

本文档是 BigOS 在当前有界用户态基线之后的规划入口。它只在高层概述已完成能力，
并为后续工作留出清晰入口。它不替代详细架构、实现、验证或变更追踪文档。

Project goal: grow BigOS from the current x86_64 research kernel into a
more general-purpose, POSIX-compatible, multi-architecture kernel. The current
runnable implementation remains x86_64-only and tied to the existing legacy
boot/storage path.

项目目标：将 BigOS 从当前 x86_64 研究内核逐步推进为更通用、兼容 POSIX、支持多架构
的内核。当前可运行实现仍是 x86_64-only，并绑定在现有 legacy boot/storage 路径上。

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
  time/identity primitives, and image replacement.
- 有界进程与 syscall 层，包括进程生命周期管理、基于文件描述符的 I/O、匿名 demand
  paging、`fork`/COW、signals、time/identity 原语和进程镜像替换。
- A minimal storage and filesystem layer with synchronous block I/O, read-only
  boot assets, a bounded writable runtime area, page/buffer cache, pipes, and fd
  duplication.
- 最小存储与文件系统层，包括同步块 I/O、只读启动资产、有界可写运行时区域、
  page/buffer cache、pipe 和 fd duplication。
- A minimal freestanding userland with resident init behavior, an interactive
  text-console shell, basic libc-style support, and small packaged user programs.
- 最小 freestanding 用户态，包括常驻 init 行为、交互式文本控制台 shell、基础 libc
  风格支持和小型用户程序。

## Current Boundary / 当前边界

BigOS is a controlled research kernel, not a complete general-purpose OS. Keep
future planning and documentation within these boundaries until a specific stage
changes them:

BigOS 是受控研究内核，不是完整通用 OS。在新的阶段明确改变前，后续规划和文档都应保持
以下边界：

- Current runnable backend: x86_64 with the existing Legacy BIOS style boot
  flow; UEFI and additional architectures are not yet runtime-parity backends.
- 当前可运行 backend：x86_64 与现有 Legacy BIOS 风格启动流程；UEFI 和其他架构尚不具备
  运行时等价能力。
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
- Boot/backends: additional boot, storage, device, and ISA backends are planning
  or parallel-track items, not current runtime parity.
- 启动/backend：额外 boot、storage、device 和 ISA backend 是规划或并行轨道事项，
  不具备当前运行时等价能力。

The roadmap should stay at this boundary level. Detailed boot order, concrete
entry points, validation markers, build commands, and change-tracking history
belong in dedicated documentation and source-adjacent notes.

路线图应保持在上述边界层级。具体启动顺序、入口点、验证 marker、构建命令和变更追踪
历史应放在专门文档与贴近源码的说明中。

## Continuous Concerns / 持续性关注

Cross-cutting items with no single completion point.

无单一完成点的横切事项。

### Behavior-Assertion Testing / 行为断言测试

- Continue replacing source-string contract tests with behavior assertions from
  runtime-observable behavior. The current userland baseline makes this more
  valuable because kernel, process, filesystem, and user program behavior can be
  checked together.
- 继续用运行时可观察行为替换源码字符串契约测试。当前用户态基线让这件事更有价值，
  因为内核、进程、文件系统和用户程序行为可以被组合验证。

### Architecture Decoupling Discipline / 架构解耦纪律

- The kernel remains deeply tied to its current x86_64 backend. New roadmap work
  should avoid spreading backend-specific mechanisms further into process,
  filesystem, and userland-facing code.
- 内核仍与当前 x86_64 backend 深度绑定。新的路线图工作应避免把 backend-specific
  机制继续扩散到进程、文件系统和面向用户态的代码中。

### Documentation Discipline / 文档纪律

- Keep detailed architecture, implementation, validation, and change-tracking
  material outside the roadmap. The roadmap should describe project-level
  capabilities, gaps, planning direction, and staged priorities only.
- 将详细架构、实现、验证和变更追踪材料放在 roadmap 之外。roadmap 只描述项目级能力、
  缺口、规划方向和阶段性优先级。
- Track detailed change rationale, assumptions, non-goals, and validation notes
  outside this roadmap.
- 详细变更理由、假设、non-goals 和验证记录应在路线图之外追踪。

## Parallel Tracks / 并行轨道

### Architecture Abstraction / 架构抽象层

Goal: gradually separate kernel core concepts from backend-specific mechanisms,
without adding speculative second-backend complexity too early.

目标：逐步把内核核心概念与 backend-specific 机制解耦，但不要过早引入缺少实际消费场景的
第二 backend 复杂度。

### SMP / 对称多处理

Keep out of the main line until locking, per-CPU state, TLB shootdown, scheduling
policy, and arch interactions are explicitly designed.

在锁、per-CPU 状态、TLB shootdown、调度策略和架构交互明确设计前，不纳入主线。

### UEFI And Boot Backends / UEFI 与引导后端

Keep future boot backend work separate from runtime scheduling, process, and VM
changes. Preserve the current runnable backend until any new backend reaches
runtime parity.

将未来 boot backend 工作与 runtime 调度、进程和 VM 变更分离。在新 backend 达到运行时
等价前，保留当前可运行 backend。

### CI And Tooling / CI 与工具链

Layer validation so future work can distinguish source checks, build checks,
runtime behavior checks, and environment-dependent checks.

分层组织验证，让后续工作能区分源码检查、构建检查、运行时行为检查和依赖本地环境的检查。

### Stage 20: Interactive Console Usability / 阶段 20：交互式控制台可用性

- Status: complete. The default bounded userland shell is usable from the
  runtime text console, including visible prompts, typed input echo, and command
  output, while bounded serial/log validation remains preserved.
- 状态：已完成。默认有界用户态 shell 已可从运行时文本控制台使用，包括可见 prompt、
  输入回显和命令输出，同时保留有界串口/日志验证。
- Boundary status: default console I/O is now a user-visible interactive path.
  Preserved boundaries: no full POSIX terminal, job control, termios, SMP, or new
  boot/architecture runtime parity.
- 边界状态：默认 console I/O 现在是用户可见的交互路径。保持边界：不引入完整 POSIX
  terminal、作业控制、termios、SMP，或新的 boot/architecture 运行时等价能力。

### Stage 21: Minimal C Program Baseline / 阶段 21：最小 C 程序运行基线

- Status: complete. Simple statically linked C user programs are now a
  first-class compatibility target with a stable process entry model,
  argument/environment handoff, syscall wrappers, error reporting, basic output,
  and small packaged utilities.
- 状态：已完成。简单静态链接 C 用户程序现在是一等兼容目标，具备稳定的进程入口模型、
  参数/环境传递、syscall wrapper、错误报告、基础输出和小型打包工具。
- Boundary status: simple C programs are now a user-visible baseline. Preserved
  boundaries: no dynamic linking, shared libraries, hosted runtime, or complete
  POSIX libc.
- 边界状态：简单 C 程序现在是用户可见基线。保持边界：不引入动态链接、共享库、
  hosted runtime 或完整 POSIX libc。

### Stage 22: Minimal C Library Subset / 阶段 22：最小 C 标准库子集

- Status: complete. The freestanding userland support is now documented as a
  minimal C library subset aligned with actual kernel syscall behavior and the
  needs of simple C programs.
- 状态：已完成。freestanding 用户态支持现在已被文档化为最小 C 标准库子集，并与实际内核
  syscall 行为和简单 C 程序需求保持一致。
- Boundary status: libc compatibility is now a staged project baseline.
  Preserved boundaries: no locale, threads, complete hosted stdio, dynamic
  loader, or broad standards conformance claim.
- 边界状态：libc 兼容性现在是阶段性项目基线。保持边界：不引入 locale、线程、完整 hosted
  stdio、动态加载器，或广泛标准兼容声明。

### Stage 23: POSIX-Like Process And I/O Subset / 阶段 23：POSIX-like 进程与 I/O 子集

- Stabilize the bounded UNIX-like behavior around process lifecycle, image
  replacement, waiting, fd inheritance, pipes, duplication, redirection, signals,
  time, identity, and shell command execution.
- 稳定围绕进程生命周期、镜像替换、等待、fd 继承、pipe、duplication、重定向、signals、
  time、identity 和 shell 命令执行的有界 UNIX-like 行为。
- Boundary change: the POSIX-compatible goal is refined into an explicit bounded
  process/I/O subset. Preserved boundaries: no sessions, terminal process groups,
  job control, complete permissions model, or complete POSIX process model.
- 边界变化：将 POSIX-compatible 目标细化为明确的有界进程/I/O 子集。保持边界：不引入
  session、terminal process group、作业控制、完整权限模型或完整 POSIX 进程模型。

### Stage 24: Bounded Runtime Filesystem Usability / 阶段 24：有界运行时文件系统可用性

- Improve bounded writable runtime storage so simple C programs can reliably use
  documented file creation, read, write, seek, sync, directory, and removal
  behavior within explicit limits.
- 改进有界可写运行时存储，让简单 C 程序能在明确限制内可靠使用有文档描述的文件创建、
  读取、写入、seek、sync、目录和删除行为。
- Boundary change: runtime file behavior becomes part of the minimal usable
  system goal. Preserved boundaries: no persistent full writable filesystem,
  broad file-backed mapping, async I/O, or broad storage/device support.
- 边界变化：运行时文件行为成为最小可用系统目标的一部分。保持边界：不引入持久完整可写
  文件系统、广泛 file-backed mapping、async I/O 或广泛存储/设备支持。

### Stage 25: x86_64/Core Decoupling Discipline / 阶段 25：x86_64 与内核核心解耦纪律

- Separate existing x86_64 backend mechanisms from kernel core concepts at real
  consumption points, preparing for future multi-architecture work without adding
  a speculative second runnable backend.
- 在真实消费点将现有 x86_64 backend 机制与内核核心概念解耦，为未来多架构工作做准备，
  但不引入缺少实际消费场景的第二可运行 backend。
- Boundary change: architecture abstraction becomes an active maintenance
  discipline. Preserved boundaries: the runnable backend remains x86_64 with the
  existing legacy boot/storage path.
- 边界变化：架构抽象成为主动维护纪律。保持边界：当前可运行 backend 仍是 x86_64 与现有
  legacy boot/storage 路径。

### Stage 26: Behavior-Oriented Validation / 阶段 26：行为导向验证

- Promote runtime-observable behavior checks for the interactive shell, simple C
  programs, process/fd semantics, filesystem operations, and userland
  compatibility so later refactoring and backend work have regression protection.
- 推进面向运行时可观察行为的检查，覆盖交互式 shell、简单 C 程序、进程/fd 语义、文件系统
  操作和用户态兼容性，为后续重构与 backend 工作提供回归保护。
- Boundary change: behavior assertions become the preferred validation direction
  for the minimal usable system. Preserved boundaries: environment-dependent
  emulator and hardware checks remain layered rather than mandatory for every
  change.
- 边界变化：行为断言成为最小可用系统的优先验证方向。保持边界：依赖环境的模拟器和硬件
  检查保持分层，而不是每次变更都强制要求。
