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
and tied to the existing legacy boot/storage path.

项目目标：将 BigOS 从当前 x86_64 研究内核逐步推进为更通用、支持多架构，并具备明确有界
POSIX-like 兼容子集的内核。当前可运行实现仍是 x86_64-only，并绑定在现有 legacy
boot/storage 路径上。

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

## Completed Capability Baseline / 已完成能力基线

Stages 20 through 26 are complete and now form a compressed minimal usable system
baseline. Keep the detailed implementation and validation history in dedicated
architecture docs and OpenSpec records; this roadmap tracks only project-level
capabilities and boundaries.

阶段 20 到阶段 26 已完成，并共同形成压缩后的最小可用系统基线。详细实现与验证历史应保留在
专门架构文档和 OpenSpec 记录中；本 roadmap 只跟踪项目级能力与边界。

- Interactive console usability: the default bounded userland shell is usable
  through the runtime text console. Preserved boundaries: no full POSIX terminal,
  job control, termios, SMP, or new boot/architecture runtime parity.
- 交互式控制台可用性：默认有界用户态 shell 可通过运行时文本控制台使用。保持边界：
  不引入完整 POSIX terminal、作业控制、termios、SMP，或新的 boot/architecture
  运行时等价能力。
- Minimal C program and libc subset: simple static freestanding C user programs
  and the repository's bounded libc-style support are part of the user-visible
  baseline. Preserved boundaries: no dynamic linking, shared libraries, hosted
  runtime, complete POSIX libc, threads, locale, complete hosted stdio, dynamic
  loader, or broad standards conformance claim.
- 最小 C 程序与 libc 子集：简单静态 freestanding C 用户程序和仓库内有界 libc 风格支持
  已是用户可见基线。保持边界：不引入动态链接、共享库、hosted runtime、完整 POSIX libc、
  线程、locale、完整 hosted stdio、动态加载器或广泛标准兼容声明。
- Bounded POSIX-like process and I/O subset: process lifecycle, image
  replacement, wait/exit behavior, fd inheritance, pipes, duplication,
  redirection, signals, time, identity, and shell command execution are bounded
  compatibility targets. Preserved boundaries: no sessions, terminal process
  groups, job control, complete permissions model, or complete POSIX process
  model.
- 有界 POSIX-like 进程与 I/O 子集：进程生命周期、镜像替换、wait/exit 行为、fd 继承、
  pipe、duplication、重定向、signals、time、identity 和 shell 命令执行是有界兼容目标。
  保持边界：不引入 session、terminal process group、作业控制、完整权限模型或完整
  POSIX 进程模型。
- Bounded runtime filesystem usability: runtime file behavior is part of the
  minimal usable system baseline for simple programs. Preserved boundaries: no
  persistent full writable filesystem, broad file-backed mapping, async I/O, or
  broad storage/device support.
- 有界运行时文件系统可用性：运行时文件行为已成为简单程序最小可用系统基线的一部分。
  保持边界：不引入持久完整可写文件系统、广泛 file-backed mapping、async I/O 或广泛
  存储/设备支持。
- x86_64/core decoupling discipline: architecture abstraction is an active
  maintenance discipline at real consumption points. Preserved boundaries: the
  runnable backend remains x86_64 with the existing legacy boot/storage path.
- x86_64 与内核核心解耦纪律：架构抽象已是在真实消费点执行的主动维护纪律。保持边界：
  当前可运行 backend 仍是 x86_64 与现有 legacy boot/storage 路径。
- Behavior-oriented validation: runtime-observable behavior assertions are the
  preferred regression-protection direction for the minimal usable system.
  Preserved boundaries: environment-dependent emulator and hardware checks remain
  layered rather than mandatory for every change.
- 行为导向验证：面向运行时可观察行为的断言是最小可用系统的优先回归保护方向。保持边界：
  依赖环境的模拟器和硬件检查保持分层，而不是每次变更都强制要求。
