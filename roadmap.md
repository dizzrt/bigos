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
  boot assets, bounded writable `/rw` backends including RAM-backed runtime
  storage and persistent clean-sync storage, constrained rename, page/buffer
  cache, pipes, fd duplication, relative path resolution, and bounded
  file/directory metadata queries.
- 最小存储与文件系统层，包括同步块 I/O、只读启动资产、包含 RAM-backed 运行期存储与
  persistent clean-sync 存储的有界可写 `/rw` 后端、受限 rename、page/buffer cache、pipe、
  fd duplication、相对路径解析和有界文件/目录元数据查询。
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
- Memory/file model: bounded anonymous demand paging/COW, bounded writable
  runtime storage, and bounded persistent clean-sync `/rw` storage, but no broad
  file-backed `mmap`, no complete POSIX filesystem, no journaling or crash
  recovery, no async I/O, and no broad storage/device support.
- 内存/文件模型：bounded anonymous demand paging/COW、有界可写运行期存储，以及有界
  persistent clean-sync `/rw` 存储，但无广泛 file-backed `mmap`、无完整 POSIX filesystem、
  无 journaling 或 crash recovery、无 async I/O、无广泛存储/设备支持。
- Boot/backends: the UEFI backend is a completed x86_64 boot spike, while
  storage, device, ISA backends, and UEFI runtime parity remain future or
  parallel-track items.
- 启动/backend：UEFI backend 已完成 x86_64 boot spike；storage、device、ISA
  backend 以及 UEFI 运行时等价能力仍是后续或并行轨道事项。

## Completed Capability Baseline / 已完成能力基线

the relevant capabilities through 44 are complete and now form a compressed minimal usable
system baseline. The completed work can be treated as the foundation for future
planning rather than as individual future-stage items.

TTY console input capability0 到persistent clean-sync `/rw` storage 已完成，并共同形成压缩后的最小可用系统基线。后续规划应将这些工作视为
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
  storage, bounded persistent clean-sync `/rw` storage, constrained rename,
  page/buffer cache, file and directory metadata, per-process current
  directories, relative path resolution, and small packaged path tools.
- 文件系统与路径基线：只读启动资产、有界可写运行时存储、有界 persistent clean-sync `/rw`
  存储、受限 rename、page/buffer cache、文件与目录 metadata、每进程 current directory、
  相对路径解析和小型打包路径工具。
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

Completed stages are intentionally compressed into the completed capability
baseline above. the relevant capabilities through 44 should no longer be treated as separate
future-planning items; their outcomes are now part of the current bounded
userland, filesystem, process, VM, and persistent storage baseline.

已完成阶段已在上方“已完成能力基线”中压缩归纳。bounded POSIX-like surface 到persistent clean-sync `/rw` storage 不应再作为独立的
未来规划项展开；其结果现在属于当前有界用户态、文件系统、进程、VM 与持久存储基线的一部分。

### Mainline Strategy / 主线策略

The next phase follows a capability-first strategy: keep the single-core
x86_64 kernel as the delivery target and grow user-visible maturity first, then
sequence real multi-core execution as the final milestone once the affected
subsystems have matured. New SMP-sensitive code added along the way should be
designed against the existing SMP preparation boundaries so the later
multi-core milestone is a controlled activation rather than a broad retrofit.

下一阶段采用功能优先策略：保持单核 x86_64 内核为交付目标，先推进用户可见的成熟度，
再将真正的多核执行安排为最后一个里程碑，待相关子系统成熟后启用。沿途新增的 SMP 敏感
代码应按既有 SMP 准备边界设计，使后续多核里程碑成为受控启用，而非大范围 retrofit。

The mainline is organized into five milestones (M1–M5) on top of the completed
capability baseline. Each milestone lists its tasks. M1 and M2 are high
priority, M3 and M4 are medium priority, and M5 is the sequenced closing
milestone.

主线在已完成能力基线之上划分为五个里程碑（M1–M5），每个里程碑列出其任务。M1 与 M2 为高
优先级，M3 与 M4 为中优先级，M5 为时序靠后的收尾里程碑。

### Milestone M1 — Address Space And mmap Maturity (High Priority) / 里程碑 M1 — 地址空间与 mmap 完善（高优先级）

User-visible goal: user programs can map files and larger anonymous regions,
enabling more capable programs.

用户可见目标：用户程序可以映射文件与更大的匿名区域，支撑更复杂的程序。

- [x] Task M1.1: file-backed read mapping built on the existing demand paging and
  page/buffer cache, without implying broad POSIX `mmap` completeness.
- [x] 任务 M1.1：在现有 demand paging 与 page/buffer cache 之上提供 file-backed 读映射，
  不暗示完整 POSIX `mmap` 语义。
- [x] Task M1.2: bounded anonymous mapping lifecycle completion covering map, unmap,
  and protection change over the current VMA model.
- [x] 任务 M1.2：在现有 VMA 模型上完善有界匿名映射生命周期，覆盖映射、解除映射与权限变更。
- [x] Task M1.3: shared read-only mapping so multiple processes can share read-only
  text and data pages, with TLB invalidation expressed through the existing SMP
  preparation invalidation boundary.
- [x] 任务 M1.3：共享只读映射，使多进程可共享只读 text 与数据页，TLB 失效通过既有 SMP 准备
  失效边界表达。

### Milestone M2 — Real Writable Filesystem (High Priority) / 里程碑 M2 — 真实可写文件系统（高优先级）

User-visible goal: the shell can create and remove directories and reliably
persist multiple files beyond the current clean-sync boundary.

用户可见目标：shell 能创建/删除目录并可靠持久地写多文件，突破当前 clean-sync 边界限制。

- [x] Task M2.1: writable directory tree supporting directory creation/removal and
  creation/removal of multiple files within bounded semantics.
- [x] 任务 M2.1：可写目录树，在有界语义内支持目录创建/删除与多文件创建/删除。
- [x] Task M2.2: file extension write, truncate, and stable block allocation.
- [x] 任务 M2.2：文件扩展写、truncate 与稳定的块分配。
- [x] Task M2.3: metadata persistence and a minimal consistency strategy such as
  bounded journaling or ordered writes, without claiming full crash recovery.
- [x] 任务 M2.3：元数据持久化与最小一致性策略（如有界 journaling 或 ordered write），
  不声称完整 crash recovery。
- [x] Task M2.4: writeback path through the page/buffer cache so synchronization
  durably reaches the backing store.
- [x] 任务 M2.4：打通经 page/buffer cache 的回写路径，使同步操作可靠落盘。

### Milestone M3 — Block Layer And Device Framework (Medium Priority) / 里程碑 M3 — 块层与设备框架（中优先级）

User-visible goal: the kernel gains an extensible device model and block I/O is
no longer constrained to a single synchronous path.

用户可见目标：内核具备可扩展设备模型，块 I/O 不再受限于单一同步路径。

- [x] Task M3.1: a freestanding-safe device and driver registration/probe framework.
- [x] 任务 M3.1：freestanding-safe 的设备与驱动注册/探测框架。
- [x] Task M3.2: a block-layer abstraction with request queueing and cache
  integration, leaving room for future async I/O.
- [x] 任务 M3.2：具备请求排队与缓存对接的块层抽象，为后续 async I/O 预留空间。
- [x] Task M3.3: a second block-device backend as a framework validation, without
  adding a new ISA.
- [x] 任务 M3.3：以第二个块设备后端作为框架验证，不接入新 ISA。

### Milestone M4 — Process POSIX Subset And libc Maturity (Medium Priority) / 里程碑 M4 — 进程 POSIX 子集与 libc 成熟（中优先级）

User-visible goal: the shell supports job-control-like interaction and more
standard small programs can compile and run directly.

用户可见目标：shell 支持作业控制类交互，更多标准小程序可直接编译运行。

- [x] Task M4.1: process group, session, and foreground terminal binding implemented
  on the existing terminal abstraction within its bounded contract.
- [x] 任务 M4.1：在既有终端抽象的有界契约内，实现进程组、session 与前台终端绑定。
- [x] Task M4.2: syscall surface expansion covering wait variants and additional
  bounded file and process primitives.
- [x] 任务 M4.2：扩充 syscall 面，覆盖 wait 变体与更多有界文件/进程原语。
- [x] Task M4.3: libc subset maturity toward portable small programs while staying
  freestanding-safe.
- [x] 任务 M4.3：推进 libc 子集成熟度以支持可移植小程序，同时保持 freestanding-safe。

### Milestone M5 — Real Multi-Core Execution (Closing Milestone) / 里程碑 M5 — 真实多核执行（收尾里程碑）

User-visible goal: the kernel schedules across multiple cores and throughput
scales with core count.

用户可见目标：内核跨多核调度，吞吐随核数提升。

- [x] Task M5.1: application processor startup with LAPIC/IOAPIC and per-CPU timers.
- [x] 任务 M5.1：AP 启动，配合 LAPIC/IOAPIC 与 per-CPU 定时器。
- [ ] Task M5.2: per-CPU run queues with cross-CPU scheduling and wakeups.
- [ ] 任务 M5.2：per-CPU run queue，支持跨核调度与唤醒。
- [ ] Task M5.3: activation of IRQ-safe locking, IPI delivery, and cross-CPU TLB
  shootdown, fulfilling the SMP preparation contracts.
- [ ] 任务 M5.3：启用 IRQ-safe 锁、IPI 投递与跨核 TLB shootdown，兑现 SMP 准备契约。
- [ ] Task M5.4: APIC-backed default interrupt delivery and review of any
  user-visible ABI changes.
- [ ] 任务 M5.4：APIC-backed 默认中断投递，并评审任何用户可见 ABI 变化。

### Parallel Foundations / 并行基础方向

- Backend work may continue for UEFI runtime parity and future backend cleanup,
  but the short-term plan does not add a new ISA.
- backend 工作可以继续推进 UEFI 运行时等价和后续 backend 清理，但短期计划不接入新 ISA。
- All x86_64 work should avoid spreading architecture-specific assumptions into
  process, filesystem, userland ABI, and generic kernel policy.
- 所有 x86_64 工作都应避免把 architecture-specific 假设扩散到进程、文件系统、用户态 ABI 和通用
  内核策略中。
