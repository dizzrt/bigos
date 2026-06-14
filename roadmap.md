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

Stages 20 through 35 are complete and now form a compressed minimal usable system
baseline. Keep the detailed implementation and validation history in dedicated
architecture docs and OpenSpec records; this roadmap tracks only project-level
capabilities and boundaries.

阶段 20 到阶段 35 已完成，并共同形成压缩后的最小可用系统基线。详细实现与验证历史应保留在
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
  minimal usable system baseline for simple programs, including constrained
  rename inside the writable runtime area, stable observable errno behavior,
  permission and capacity failure boundaries, and consistent runtime metadata
  visibility. Preserved boundaries: no persistent full writable filesystem,
  broad file-backed mapping, async I/O, or broad storage/device support.
- 有界运行时文件系统可用性：运行时文件行为已成为简单程序最小可用系统基线的一部分。
  其中包括可写运行时区域内的受限 rename、稳定可观察 errno 行为、权限与容量失败边界，
  以及一致的运行时 metadata 可见性。保持边界：不引入持久完整可写文件系统、广泛
  file-backed mapping、async I/O 或广泛存储/设备支持。
- Minimal metadata contract: simple programs can observe bounded file and
  directory metadata through kernel, libc, user-tool, and behavior-validation
  paths. Preserved boundaries: no symbolic links, device-node model, complete
  POSIX metadata database, stable inode identity, ACLs, extended attributes, or
  broad standards-conformance claim.
- 最小元数据契约：简单程序可通过内核、libc、用户工具和行为验证路径观察有界文件与目录
  元数据。保持边界：不引入符号链接、设备节点模型、完整 POSIX 元数据数据库、稳定 inode
  身份、ACL、扩展属性或广泛标准兼容声明。
- Cwd and relative path handling: simple programs and the bounded shell can use
  per-process current directories, relative path resolution, and POSIX-style `.`
  and `..` components across supported path-taking operations. Preserved
  boundaries: no mount namespaces, `chroot`, symbolic links, or complete
  path-canonicalization semantics.
- Cwd 与相对路径处理：简单程序和有界 shell 可在受支持的路径操作中使用每进程 current
  directory、相对路径解析，以及 POSIX-style `.` 和 `..` 组件。保持边界：不引入 mount
  namespace、`chroot`、符号链接或完整路径规范化语义。
- Bounded userland path tools: small packaged tools make directory listing, file
  content viewing, metadata observation, writable runtime path creation/removal,
  constrained rename, and shell composition observable from the bounded
  userland. Preserved boundaries: no complete POSIX utility suite, recursive
  traversal, globbing, scripting environment, locale-aware formatting, dynamic
  linking, hosted libc, or complete POSIX shell behavior.
- 有界用户态路径工具：小型打包工具让目录列举、文件内容查看、元数据观察、可写运行时路径
  创建/删除、受限 rename 和 shell 组合行为可从有界用户态观察。保持边界：不引入完整
  POSIX 工具集、递归遍历、globbing、脚本环境、locale-aware 格式化、动态链接、hosted
  libc 或完整 POSIX shell 行为。
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

## Future Mainline / 后续主线

The next stages should keep the roadmap at capability-planning granularity. Each
stage should close one observable kernel-to-userland capability loop: kernel
contract, freestanding libc exposure, shell or packaged user-program consumption,
and behavior-oriented validation. Detailed syscall numbers, source entry points,
commands, markers, and validation logs belong in OpenSpec changes, architecture
docs, or source-adjacent notes rather than this roadmap.

后续阶段应继续保持 capability-planning 粒度。每个阶段应闭合一个可观察的
kernel-to-userland 能力环：内核契约、freestanding libc 暴露、shell 或打包用户程序消费、
以及行为导向验证。具体 syscall 编号、源码入口、命令、marker 和验证日志应放在
OpenSpec change、架构文档或贴近源码的说明中，而不是放在本 roadmap 中。

### Stage 28: Cwd And Relative Paths / Cwd 与相对路径

- Status: complete; this capability is now part of the completed baseline rather
  than future mainline scope.
- 状态：已完成；该能力现在属于已完成基线，不再属于后续主线范围。
- The current baseline includes a per-process current-directory contract and
  relative path resolution across supported path-taking kernel and userland
  interfaces.
- 当前基线包括每进程 current-directory 契约，并让受支持的接收路径的内核与用户态接口具备一致的
  相对路径解析。
- Preserved boundaries: no mount namespaces, `chroot`, symlink traversal, or
  complete path-canonicalization semantics.
- 保持边界：不引入 mount namespace、`chroot`、符号链接遍历或完整路径规范化语义。

### Stage 29: Userland Path Tools / 用户态路径工具

- Status: complete; this capability is now part of the completed baseline rather
  than future mainline scope.
- 状态：已完成；该能力现在属于已完成基线，不再属于后续主线范围。
- The current baseline includes small packaged path tools for bounded directory
  listing, file content viewing, metadata observation, writable runtime path
  changes, and shell composition.
- 当前基线包括用于有界目录列举、文件内容查看、元数据观察、可写运行时路径变更和 shell 组合的
  小型打包路径工具。
- Preserved boundaries: no complete POSIX utility suite, recursive traversal,
  globbing, scripting environment, locale-aware formatting, dynamic linking,
  hosted libc, or complete POSIX shell behavior.
- 保持边界：不引入完整 POSIX 工具集、递归遍历、globbing、脚本环境、locale-aware 格式化、
  动态链接、hosted libc 或完整 POSIX shell 行为。

### Stage 30: Constrained Rename / 受限 Rename

- Status: complete; this capability is now part of the completed baseline rather
  than future mainline scope.
- 状态：已完成；该能力现在属于已完成基线，不再属于后续主线范围。
- The current baseline includes bounded rename for regular files in the writable
  runtime filesystem, with libc and userland tool exposure.
- 当前基线包括可写运行时文件系统内常规文件的有界 rename，并已通过 libc 与用户态工具暴露。
- Preserved boundaries: no hard links, symbolic links, cross-mount rename, full
  directory rename semantics, or full POSIX atomic-replacement guarantee.
- 保持边界：不引入硬链接、符号链接、跨挂载 rename、完整目录 rename 语义或完整 POSIX
  atomic replacement 保证。

### Stage 31: Runtime Filesystem Semantics / 运行时文件系统语义

- Status: complete; this capability is now part of the completed baseline rather
  than future mainline scope.
- 状态：已完成；该能力现在属于已完成基线，不再属于后续主线范围。
- The current baseline includes hardened observable filesystem errors, metadata
  consistency, permissions edges, directory behavior, and read-only versus
  writable backend differences.
- 当前基线包括已硬化的可观察文件系统错误、元数据一致性、权限边界、目录行为，以及只读
  backend 与可写 backend 的差异。
- Preserved boundaries: no journaling, cross-reboot persistence, ACLs, extended
  attributes, broad file-backed mappings, async I/O, or broad storage or device
  support.
- 保持边界：不引入 journaling、跨重启持久化、ACL、扩展属性、广泛 file-backed mapping、
  async I/O 或广泛存储/设备支持。

### Stage 32: Shell Usability Hardening / Shell 可用性硬化

- Status: complete; this capability is now part of the completed baseline rather
  than future mainline scope.
- 状态：已完成；该能力现在属于已完成基线，不再属于后续主线范围。
- Improve the bounded interactive shell experience around path handling, error
  reporting, exit-status propagation, pipes, redirection, and small utility
  composition.
- 改善有界交互式 shell 在路径处理、错误展示、退出状态传播、pipe、重定向和小工具组合方面的
  使用体验。
- Preserve the existing boundary: no job control, terminal process groups,
  sessions, complete terminal control, or complete POSIX shell language.
- 保持既有边界：不引入作业控制、terminal process group、session、完整终端控制或完整
  POSIX shell 语言。

### Stage 33: Syscall And User ABI Boundary / Syscall 与用户 ABI 边界

- Status: complete; this capability is now part of the completed baseline rather
  than future mainline scope.
- 状态：已完成；该能力现在属于已完成基线，不再属于后续主线范围。
- The current baseline tightens the architecture boundary around syscall ABI,
  user-visible register conventions, user headers, and kernel/user contract
  documentation.
- 当前基线已收紧 syscall ABI、用户可见寄存器约定、用户态头文件和 kernel/user 契约文档周围的
  架构边界。
- Preserved boundaries: no second runtime backend, no new ISA parity, and no
  broader POSIX or hosted libc compatibility claim.
- 保持边界：不引入第二个运行时 backend、新 ISA 运行时等价能力，或更广的 POSIX/hosted
  libc 兼容声明。

### Stage 34: Interrupt, Timer, And Context Boundary / 中断、计时与上下文边界

- Status: complete; this capability is now part of the completed baseline rather
  than future mainline scope.
- 状态：已完成；该能力现在属于已完成基线，不再属于后续主线范围。
- Separate interrupt, timer, context-switch, and scheduler-facing architecture
  mechanisms from portable kernel policy at real consumption points.
- 在真实消费点上，将中断、计时、上下文切换和调度器面对的架构机制与可移植内核策略分离。
- Preserve the current single-core execution model and avoid folding SMP or new
  backend runtime parity into this cleanup stage.
- 保持当前单核执行模型，避免把 SMP 或新 backend 运行时等价能力混入本清理阶段。

### Stage 35: VM And User-Entry Boundary / VM 与用户态入口边界

- Status: complete; this capability is now part of the completed baseline rather
  than future mainline scope.
- 状态：已完成；该能力现在属于已完成基线，不再属于后续主线范围。
- Clarify the boundaries between core virtual-memory policy, address-space
  switching, user-entry mechanics, and architecture-specific fault handling.
- 明确 core 虚拟内存策略、地址空间切换、用户态入口机制和架构特定 fault handling 之间的边界。
- Keep this as architecture-boundary hardening, not a broad file-backed `mmap`,
  dynamic-linking, or second-architecture implementation stage.
- 将本阶段保持为架构边界硬化，而不是广泛 file-backed `mmap`、动态链接或第二架构实现阶段。

### Stage 36: Backend Expansion Spike / Backend 扩展试探

- Choose one real backend-expansion spike only after the preceding architecture
  boundaries have enough real consumers. A modern x86_64 boot backend is the
  lower-risk path; a second ISA spike is the stronger test of multi-architecture
  assumptions.
- 只有在前序架构边界已有足够真实消费点之后，才选择一个真实 backend 扩展试探。现代 x86_64
  boot backend 风险较低；第二 ISA 试探更能检验多架构假设。
- Preserve the current runnable backend until any new backend reaches explicit
  runtime parity.
- 在任何新 backend 明确达到运行时等价前，保留当前可运行 backend。

### Stage 37: Terminal Preparation / 终端能力准备

- Prepare a minimal terminal abstraction for future interactive work, including
  bounded input ownership and control-character semantics where they are needed
  by the shell and user programs.
- 为后续交互能力准备最小终端抽象，包括 shell 与用户程序需要的有界输入归属和控制字符语义。
- Do not treat this as full terminal control, sessions, job control, process
  groups, or complete POSIX terminal support.
- 不将本阶段视为完整终端控制、session、作业控制、process group 或完整 POSIX terminal 支持。

### Stage 38: SMP Preparation / SMP 准备

- Design and stage the locking model, per-CPU state, scheduler boundaries,
  interrupt-routing assumptions, TLB shootdown requirements, and memory-ordering
  rules needed before enabling real SMP execution.
- 设计并分阶段引入真正启用 SMP 前所需的锁模型、per-CPU 状态、调度器边界、中断路由假设、
  TLB shootdown 要求和内存序规则。
- Keep real multi-core execution disabled until these assumptions are explicit and
  validated on the single-core baseline.
- 在这些假设明确并通过单核基线验证前，保持真正的多核执行关闭。

### Stage 39 And Later / Stage 39 及之后

- Pick one major expansion at a time after the foundation above: real SMP,
  dynamic linking, a persistent writable filesystem, broader POSIX compatibility
  families, or additional runtime-parity backends.
- 在上述基础完成后，一次只选择一个大型扩展方向：真正 SMP、动态链接、持久可写文件系统、
  更广的 POSIX 兼容能力族，或更多运行时等价 backend。
- Continue to state compatibility as explicit bounded subsets until the project
  intentionally changes its maturity target.
- 在项目有意改变成熟度目标前，继续将兼容性表述为明确有界子集。
