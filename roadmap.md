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
compatibility subsets. The current runnable implementation remains x86_64-only,
now multi-core capable, with the x86_64 UEFI boot backend as the default
bounded-userland baseline and the existing Legacy BIOS boot/storage path kept as
an explicit compatibility backend.

项目目标：将 BigOS 从当前 x86_64 研究内核逐步推进为更通用、支持多架构，并具备明确有界
POSIX-like 兼容子集的内核。当前可运行实现仍是 x86_64-only，现已具备多核能力，x86_64
UEFI boot backend 是默认有界用户态基线，现有 Legacy BIOS boot/storage 路径作为显式兼容
backend 保留。

## Current Implementation Summary / 当前实现概述

BigOS currently provides a smoke-tested, multi-core capable kernel with a
bounded userland. At a high level, the implemented baseline includes:

BigOS 当前提供一个经过 smoke 验证、具备多核能力、具备有界用户态的内核。从高层看，
当前已实现基线包括：

- A working x86_64 boot and kernel runtime foundation with text/serial output,
  interrupt/exception/syscall dispatch, basic timers, keyboard input, and a
  multi-core scheduler.
- 可工作的 x86_64 启动与内核运行基础，包括文本/串口输出、中断/异常/syscall 分发、
  基础计时、键盘输入和多核调度。
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

- Current runnable backends: x86_64 UEFI is the default bounded-userland boot
  backend; the existing Legacy BIOS style boot flow remains an explicit
  compatibility backend. Additional architectures are not yet runtime-parity
  backends.
- 当前可运行 backend：x86_64 UEFI 是默认有界用户态启动 backend；现有 Legacy BIOS 风格
  启动流程仍作为显式兼容 backend 保留。其他架构尚不具备运行时等价能力。
- Execution model: multi-core capable with per-CPU scheduling and cross-CPU
  coordination, mostly synchronous I/O, bounded userland.
- 执行模型：具备多核能力，支持 per-CPU 调度与跨核协同，I/O 以同步为主，有界用户态。
- Userland: minimal static user programs and a bounded interactive console path,
  no dynamic linking/shared libraries, no job control, no terminal process
  groups, no complete POSIX libc.
- 用户态：最小静态用户程序与有界交互式控制台路径，无动态链接/共享库、无作业控制、
  无终端进程组、无完整 POSIX libc。
- Display: a single text-only console on the Legacy BIOS VGA text path with
  bounded scrollback; no graphical framebuffer rendering and no non-ASCII glyph
  rendering yet.
- 显示：仅有 Legacy BIOS VGA 文本路径上的单一文本控制台，具备有界 scrollback；尚无
  图形 framebuffer 渲染，也尚无非 ASCII 字形渲染。
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
- Boot/backends: the UEFI backend is the default x86_64 bounded-userland boot
  backend. Broader firmware parity, storage, device, and ISA backends remain
  future or parallel-track items.
- 启动/backend：UEFI backend 是默认 x86_64 有界用户态启动 backend。更广泛的 firmware
  parity、storage、device、ISA backend 仍是后续或并行轨道事项。

## Completed Capability Baseline / 已完成能力基线

The kernel foundation, bounded userland, filesystem/path layer, and the M1–M5
mainline milestones are complete and now form a compressed minimal usable
system baseline. The completed work can be treated as the foundation for future
planning rather than as individual future-stage items.

内核基础、有界用户态、文件系统/路径层，以及 M1–M5 主线里程碑均已完成，并共同形成压缩后
的最小可用系统基线。后续规划应将这些工作视为基础能力，而不是继续把它们作为未来阶段
逐项展开。

- Kernel foundation: x86_64 legacy boot, text/serial output,
  interrupt/exception/syscall dispatch, timer and keyboard input, multi-core
  scheduling, kernel memory management, user address spaces, and bounded user
  fault handling.
- 内核基础：x86_64 legacy boot、文本/串口输出、中断/异常/syscall 分发、计时器与键盘输入、
  多核调度、内存管理、用户地址空间和有界用户 fault 处理。
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
- Mainline M1–M5 capabilities: file-backed read mapping and a bounded anonymous
  mapping lifecycle, a writable directory tree with stable file growth and
  metadata consistency, a device/driver framework with a queued block I/O layer
  and a second block backend, a bounded process/session/terminal and syscall/libc
  subset, and real multi-core execution with per-CPU run queues, typed IPI
  delivery, cross-CPU TLB shootdown, and APIC-backed interrupt delivery.
- 主线 M1–M5 能力：file-backed 读映射与有界匿名映射生命周期；具备稳定文件增长与元数据
  一致性的可写目录树；具备排队块 I/O 层与第二块设备后端的设备/驱动框架；有界进程/会话/
  终端与 syscall/libc 子集；以及具备 per-CPU run queue、类型化 IPI 投递、跨核 TLB
  shootdown 与 APIC-backed 中断投递的真实多核执行。
- Expansion foundations: the default x86_64 UEFI boot backend, initial
  x86_64/core decoupling, and terminal preparation.
- 扩展基础：默认 x86_64 UEFI boot backend、初步 x86_64/core 解耦，以及终端能力准备。

## Future Mainline / 后续主线

Future work should move BigOS toward a more mature and usable system while
preserving the current x86_64-only delivery target. Multiple expansion tracks
may progress in parallel, but each mainline stage should still have a clear
user-visible capability goal.

后续工作应推动 BigOS 走向更成熟、可用的系统，同时保持当前 x86_64-only 的交付目标。
多个扩展方向可以并行推进，但每个主线阶段仍应有清晰的用户可见能力目标。

Completed stages are intentionally compressed into the completed capability
baseline above. The M1–M5 milestones should no longer be treated as separate
future-planning items; their outcomes are now part of the current bounded
userland, filesystem, process, VM, multi-core, and persistent storage baseline.

已完成阶段已在上方“已完成能力基线”中压缩归纳。M1–M5 里程碑不应再作为独立的未来规划项
展开；其结果现在属于当前有界用户态、文件系统、进程、VM、多核与持久存储基线的一部分。

### Mainline Strategy / 主线策略

The next phase keeps x86_64 as the delivery target and grows user-visible
maturity in dependency order: first modernize the boot backend and display
subsystem, then consolidate the freshly landed multi-core base, then move I/O
toward asynchronous and interrupt-driven paths so modern storage and networking
can build on a correct foundation, then mature the userland, and then deepen the
architecture-neutral capabilities that make the system feel usable — user-visible
asynchronous I/O and multiplexing, a connection-oriented network stack, and a
recoverable writable filesystem — before any hardening or multi-architecture
work. New code added along the way should respect the existing boot, memory,
interrupt, and multi-core boundaries so each milestone stays a controlled
extension rather than a broad retrofit. After the depth phase the continuation
phase stays on x86_64 and raises general-purpose maturity in dependency order:
first let user programs use the multi-core kernel through bounded user-space
parallelism, then harden isolation and resource limits whose threat model that
parallelism makes concrete, then mature the ecosystem so more standard programs
run, and broaden device support as a parallel track. Multi-architecture work is
deferred to a later long-term goal.

下一阶段保持 x86_64 为交付目标，并按依赖顺序推进用户可见成熟度：先现代化启动 backend
与显示子系统，再巩固刚落地的多核基线，随后将 I/O 推向异步与中断驱动路径，使现代存储与
网络能够构建在正确的基础之上，再完善用户态，然后在任何加固或多架构工作之前夯实让系统
真正可用的 architecture-neutral 能力——用户可见的异步 I/O 与多路复用、面向连接的网络栈，
以及可恢复的可写文件系统。沿途新增代码应尊重既有启动、内存、中断与多核边界，使每个
里程碑成为受控扩展，而非大范围 retrofit。深度阶段之后，延续阶段继续保持 x86_64，并按
依赖顺序提升通用成熟度：先通过有界用户态并行让用户程序用满多核内核，再加固隔离与资源
限制（其威胁模型正由该并行能力变得具体），随后完善生态以运行更多标准程序，并把设备支持
作为并行轨道拓宽。多架构工作延后为长期目标。

The mainline extends with fourteen sequenced milestones (M6–M19) on top of the
completed capability baseline. Each milestone has a clear user-visible goal and
lists its tasks; they are pursued in M6–M19 order, where later milestones depend
on earlier ones. M6–M12 are complete. M13–M15 form the next depth phase: they
deepen architecture-neutral capabilities (user-visible asynchronous I/O and
multiplexing, a real networking stack, and a real writable filesystem) and are
validated once on x86_64 to reduce later multi-architecture rework. M16–M19 form
the x86_64 continuation phase: they raise general-purpose maturity on a single
architecture by letting user programs use the multi-core kernel, hardening
isolation, maturing the ecosystem, and broadening device support. Multi-architecture
work is deliberately deferred to a later long-term goal so it builds on
already-mature generic capabilities rather than forcing them to be revalidated
per ISA.

主线在已完成能力基线之上扩展为十四个有序里程碑（M6–M19）。每个里程碑都有清晰的用户可见
目标并列出其任务；它们按 M6–M19 的顺序推进，后续里程碑依赖在先的里程碑。M6–M12 已完成。
M13–M15 构成下一阶段的深度推进：先夯实 architecture-neutral 能力（用户可见的异步 I/O
与多路复用、真实网络栈、真实可写文件系统），并只在 x86_64 上验证一次，以降低后续多架构
的返工成本。M16–M19 构成 x86_64 延续阶段：在单一架构上提升通用成熟度——让用户程序用满
多核内核、加固隔离、完善生态、拓宽设备支持。多架构工作被有意延后为长期目标，使其构建在
已成熟的通用能力之上，而不必为每个 ISA 重复验证。

### Milestone M1 — Address Space And mmap Maturity / 里程碑 M1 — 地址空间与 mmap 完善

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

### Milestone M2 — Real Writable Filesystem / 里程碑 M2 — 真实可写文件系统

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

### Milestone M3 — Block Layer And Device Framework / 里程碑 M3 — 块层与设备框架

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

### Milestone M4 — Process POSIX Subset And libc Maturity / 里程碑 M4 — 进程 POSIX 子集与 libc 成熟

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

### Milestone M5 — Real Multi-Core Execution / 里程碑 M5 — 真实多核执行

User-visible goal: the kernel schedules across multiple cores and throughput
scales with core count.

用户可见目标：内核跨多核调度，吞吐随核数提升。

- [x] Task M5.1: application processor startup with LAPIC/IOAPIC and per-CPU timers.
- [x] 任务 M5.1：AP 启动，配合 LAPIC/IOAPIC 与 per-CPU 定时器。
- [x] Task M5.2: per-CPU run queues with cross-CPU scheduling and wakeups.
- [x] 任务 M5.2：per-CPU run queue，支持跨核调度与唤醒。
- [x] Task M5.3: activation of IRQ-safe locking, IPI delivery, and cross-CPU TLB
  shootdown, fulfilling the SMP preparation contracts.
- [x] 任务 M5.3：启用 IRQ-safe 锁、IPI 投递与跨核 TLB shootdown，兑现 SMP 准备契约。
- [x] Task M5.4: APIC-backed default interrupt delivery and review of any
  user-visible ABI changes.
- [x] 任务 M5.4：APIC-backed 默认中断投递，并评审任何用户可见 ABI 变化。

### Milestone M6 — UEFI As The Primary Boot Backend / 里程碑 M6 — UEFI 成为主启动 backend

User-visible goal: BigOS boots by default through UEFI to the same bounded
userland, while the Legacy BIOS path remains a runnable cross-validation backend.

用户可见目标：BigOS 默认通过 UEFI 启动并到达相同的有界用户态，同时 Legacy BIOS 路径仍
作为可运行的交叉验证 backend 保留。

- [x] Task M6.1: promote the UEFI backend from a bounded spike to the default
  runnable boot backend, reaching the existing bounded userland baseline.
- [x] 任务 M6.1：将 UEFI backend 从有界 spike 提升为默认可运行启动 backend，
  到达现有有界用户态基线。
- [x] Task M6.2: keep the Legacy BIOS boot and storage path runnable and unchanged
  as a downgraded cross-validation backend, without removing it.
- [x] 任务 M6.2：保持 Legacy BIOS 启动与存储路径可运行且不变，作为降级的交叉验证 backend
  保留，不删除。
- [x] Task M6.3: align default build, run, and headless smoke validation so the
  existing user-visible boot behavior is reproduced through the UEFI path.
- [x] 任务 M6.3：对齐默认构建、运行与 headless smoke 验证，使现有用户可见启动行为可通过
  UEFI 路径复现。

### Milestone M7 — Framebuffer Console And Unicode Text / 里程碑 M7 — Framebuffer 控制台与 Unicode 文本

User-visible goal: under UEFI the console renders text as bitmap glyphs on a
graphical framebuffer and can display non-ASCII characters such as CJK, while the
Legacy text console remains as a fallback.

用户可见目标：在 UEFI 下控制台以点阵字形在图形 framebuffer 上渲染文本，并可显示 CJK 等
非 ASCII 字符，同时保留 Legacy 文本控制台作为回退。

- [x] Task M7.1: obtain a linear framebuffer through firmware graphics output and
  pass framebuffer geometry and a font asset to the kernel through the boot
  handoff so they are available early.
- [x] 任务 M7.1：通过固件图形输出获取线性 framebuffer，并经启动握手把 framebuffer 几何信息
  与字体资产早期传给内核。
- [x] Task M7.2: a build-time font asset pipeline that converts the bundled bitmap
  font into a compact in-kernel glyph lookup covering half-width and full-width
  glyphs.
- [x] 任务 M7.2：构建期字体资产管线，将随附点阵字体转换为紧凑的内核内字形查找，覆盖半宽与
  全宽字形。
- [x] Task M7.3: a framebuffer text console backend that renders glyphs, a software
  cursor, and scrolling behind the existing console output interface so upper
  console/scrollback state stays reusable.
- [x] 任务 M7.3：framebuffer 文本控制台后端，在既有控制台输出接口之后渲染字形、软件光标与
  滚动，使上层 console/scrollback 状态可复用。
- [x] Task M7.4: upgrade the console text model to UTF-8 decoding, codepoint-based
  cells, and double-width cell handling so non-ASCII text can be displayed; the
  Legacy text backend degrades non-ASCII codepoints deterministically.
- [x] 任务 M7.4：将控制台文本模型升级为 UTF-8 解码、基于 codepoint 的 cell 与双宽 cell
  处理以显示非 ASCII 文本；Legacy 文本后端对非 ASCII codepoint 做确定性降级显示。

### Milestone M8 — Multi-Core Hardening / 里程碑 M8 — 多核加固

User-visible goal: the freshly landed multi-core base is trusted under
concurrency stress before further subsystems build on it.

用户可见目标：在更多子系统构建于其上之前，让刚落地的多核基线在并发压力下可被信任。

- [x] Task M8.1: concurrency stress and regression validation for multi-core
  scheduling, cross-CPU wakeups, IPI delivery, and TLB shootdown completion.
- [x] 任务 M8.1：针对多核调度、跨核唤醒、IPI 投递与 TLB shootdown 完成的并发压力与回归验证。
- [x] Task M8.2: audit and harden shared-state locking and ordering across affected
  scheduler, memory, and interrupt paths.
- [x] 任务 M8.2：审查并加固受影响的调度、内存与中断路径上的共享状态加锁与顺序。
- [x] Task M8.3: strengthen deterministic diagnostics and fail-closed behavior for
  multi-core fault and timeout conditions.
- [x] 任务 M8.3：增强多核故障与超时条件下的确定性诊断与 fail-closed 行为。

### Milestone M9 — Asynchronous And Interrupt-Driven I/O / 里程碑 M9 — 异步与中断驱动 I/O

User-visible goal: block I/O no longer blocks on synchronous polling, enabling
correct modern storage and networking on top.

用户可见目标：块 I/O 不再依赖同步轮询，使其上的现代存储与网络能够正确构建。

- [x] Task M9.1: an interrupt-driven I/O completion model integrated with the
  existing block request layer and scheduler wakeups.
- [x] 任务 M9.1：与既有块请求层和调度唤醒集成的中断驱动 I/O 完成模型。
- [x] Task M9.2: convert the existing block path off synchronous polling within
  bounded semantics, preserving current cache and writeback behavior.
- [x] 任务 M9.2：在有界语义内将现有块路径从同步轮询切换走，保持当前缓存与回写行为。
- [x] Task M9.3: bounded asynchronous request lifecycle and diagnostics that remain
  IRQ-safe and freestanding-safe.
- [x] 任务 M9.3：保持 IRQ-safe 与 freestanding-safe 的有界异步请求生命周期与诊断。

### Milestone M10 — Modern Storage Drivers / 里程碑 M10 — 现代存储驱动

User-visible goal: BigOS drives a modern storage device through the device and
async I/O framework, validating them with real hardware-style backends.

用户可见目标：BigOS 通过设备与异步 I/O 框架驱动现代存储设备，以真实硬件风格后端验证它们。

- [x] Task M10.1: a modern block-storage driver such as virtio-blk or NVMe built on
  the device framework and async I/O completion model.
- [x] 任务 M10.1：基于设备框架与异步 I/O 完成模型构建现代块存储驱动，如 virtio-blk 或 NVMe。
- [x] Task M10.2: integrate the new storage backend with the block layer, cache,
  and writeback path within bounded semantics.
- [x] 任务 M10.2：在有界语义内将新存储后端与块层、缓存和回写路径集成。
- [x] Task M10.3: storage driver validation through the emulator path without adding
  a new ISA.
- [x] 任务 M10.3：通过仿真器路径验证存储驱动，不接入新 ISA。

### Milestone M11 — Networking Stack / 里程碑 M11 — 网络栈

User-visible goal: user programs can perform basic network communication through
a minimal socket interface.

用户可见目标：用户程序可通过最小 socket 接口进行基础网络通信。

- [x] Task M11.1: a network device driver such as virtio-net on the device and
  interrupt-driven I/O framework.
- [x] 任务 M11.1：基于设备与中断驱动 I/O 框架的网络设备驱动，如 virtio-net。
- [x] Task M11.2: a bounded network protocol path sufficient for basic
  communication, without claiming a complete network stack.
- [x] 任务 M11.2：足以支撑基础通信的有界网络协议路径，不声称完整网络栈。
- [x] Task M11.3: a minimal user-visible socket interface integrated with the
  existing fd/syscall path within bounded semantics.
- [x] 任务 M11.3：在有界语义内与既有 fd/syscall 路径集成的最小用户可见 socket 接口。

### Milestone M12 — Userland Maturity / 里程碑 M12 — 用户态成熟度

User-visible goal: BigOS feels more like a usable system, with more standard
programs and a richer userland.

用户可见目标：BigOS 更像一个可用系统，支持更多标准程序与更丰富的用户态。

- [x] Task M12.1: dynamic linking and shared library support within bounded
  freestanding-safe semantics.
- [x] 任务 M12.1：在有界 freestanding-safe 语义内支持动态链接与共享库。
- [x] Task M12.2: a more complete libc subset toward portable standard small
  programs.
- [x] 任务 M12.2：更完整的 libc 子集，支撑可移植的标准小程序。
- [x] Task M12.3: a broader set of bounded core userland utilities.
- [x] 任务 M12.3：更广的一组有界核心用户态工具程序。

### Depth Phase / 深度阶段

M13–M15 deepen architecture-neutral capabilities before any hardening or
multi-architecture work. They build directly on the completed baseline: M13
adds a user-visible readiness and multiplexing model that later networking and
interactive programs depend on, M14 grows the bounded UDP-only network path into
a usable connection-oriented stack, and M15 turns the bounded clean-sync `/rw`
storage into a recoverable writable filesystem. All three are validated once on
the x86_64 delivery target and must avoid leaking architecture-specific
assumptions into generic process, I/O, network, and filesystem policy.

M13–M15 在任何加固或多架构工作之前，先夯实 architecture-neutral 能力。它们直接构建在
已完成基线之上：M13 增加后续网络与交互式程序所依赖的用户可见 readiness 与多路复用模型，
M14 将有界 UDP-only 网络路径成长为可用的面向连接网络栈，M15 将有界 clean-sync `/rw`
存储升级为可恢复的可写文件系统。三者都只在 x86_64 交付目标上验证一次，并且必须避免把
architecture-specific 假设扩散到通用进程、I/O、网络与文件系统策略中。

### Milestone M13 — Asynchronous I/O And Multiplexing / 里程碑 M13 — 异步 I/O 与多路复用

User-visible goal: a single-threaded user program can wait on multiple
descriptors at once and write an event loop, instead of busy-waiting one
descriptor at a time.

用户可见目标：单线程用户程序可以一次等待多个描述符并编写事件循环，而不再逐个描述符忙等。

- [x] Task M13.1: a kernel fd readiness model expressing readable, writable, and
  error readiness for socket, pipe, and terminal descriptors over the existing
  blocking primitives, without implying broad POSIX poll semantics.
- [x] 任务 M13.1：内核 fd readiness 模型，在既有阻塞原语之上为 socket、pipe 与终端描述符
  表达可读、可写与错误就绪状态，不暗示完整 POSIX poll 语义。
- [x] Task M13.2: bounded non-blocking descriptor behavior so reads and writes
  return a deterministic would-block status instead of blocking, integrated with
  the existing fd-control path.
- [x] 任务 M13.2：有界非阻塞描述符行为，使读写返回确定性 would-block 状态而非阻塞，
  并与既有 fd-control 路径集成。
- [x] Task M13.3: a bounded multiplexing syscall that waits on a fixed-capacity
  set of descriptors with a timeout and reports per-descriptor readiness, reusing
  the scheduler wait queues.
- [x] 任务 M13.3：有界多路复用 syscall，对定容描述符集合带超时等待并报告各描述符就绪状态，
  复用调度等待队列。

### Milestone M14 — Connection-Oriented Networking / 里程碑 M14 — 面向连接的网络栈

User-visible goal: user programs can run real TCP client/server exchanges,
connect to a loopback address, and resolve names, on top of the existing bounded
network path.

用户可见目标：用户程序可以在既有有界网络路径之上运行真实 TCP client/server 交互、连接
loopback 地址并解析名字。

- [x] Task M14.1: a loopback network path so connection-oriented and datagram
  traffic to the local address works without a physical or emulated network
  card, enabling reproducible default-off validation.
- [x] 任务 M14.1：loopback 网络路径，使面向连接与 datagram 的本机地址流量无需物理或仿真
  网卡即可工作，支撑可复现的默认关闭验证。
- [ ] Task M14.2: a bounded TCP path with connection setup, bounded retransmission
  and windowing, ordered delivery, and connection teardown, without claiming a
  complete TCP feature matrix.
- [ ] 任务 M14.2：有界 TCP 路径，包含连接建立、有界重传与窗口、有序交付与连接拆除，
  不声称完整 TCP 特性矩阵。
- [ ] Task M14.3: a stream socket interface integrated with the fd, multiplexing,
  and syscall paths, exposing connect, listen, and accept within bounded
  semantics.
- [ ] 任务 M14.3：与 fd、多路复用与 syscall 路径集成的 stream socket 接口，在有界语义内
  暴露 connect、listen 与 accept。
- [ ] Task M14.4: a minimal DNS client over the existing UDP path sufficient for
  basic name resolution, without a general resolver or caching daemon.
- [ ] 任务 M14.4：基于既有 UDP 路径的最小 DNS client，足以支持基础名字解析，
  不提供通用 resolver 或缓存守护进程。

### Milestone M15 — Recoverable Writable Filesystem / 里程碑 M15 — 可恢复可写文件系统

User-visible goal: the filesystem stays consistent across a crash or power loss
and can mount more than one writable backend, moving beyond the current
clean-sync boundary.

用户可见目标：文件系统在崩溃或断电后仍保持一致，并可挂载多于一个可写后端，突破当前
clean-sync 边界。

- [ ] Task M15.1: a write-ahead journaling path for the writable `/rw` backend
  with bounded log records covering metadata and data ordering, without claiming
  general POSIX durability guarantees.
- [ ] 任务 M15.1：可写 `/rw` 后端的 write-ahead journaling 路径，使用覆盖元数据与数据
  顺序的有界日志记录，不声称通用 POSIX 持久性保证。
- [ ] Task M15.2: a mount-time recovery path that replays or discards the journal
  to restore a consistent filesystem state after an unclean shutdown.
- [ ] 任务 M15.2：挂载时恢复路径，通过 replay 或丢弃 journal，在非干净关机后恢复一致的
  文件系统状态。
- [ ] Task M15.3: a bounded VFS mount framework that lets more than one writable
  filesystem backend attach at distinct mount points, preserving the read-only
  boot asset and existing `/rw` boundaries.
- [ ] 任务 M15.3：有界 VFS mount 框架，使多于一个可写文件系统后端可挂载在不同挂载点，
  同时保留只读启动资产与既有 `/rw` 边界。

### Continuation Phase / 延续阶段

M16–M19 keep x86_64 as the only delivery target and raise general-purpose
maturity on a single architecture. They build directly on the depth phase: M16
lets a user program use the multi-core kernel through bounded user-space
parallelism, M17 hardens isolation and resource limits whose threat model that
parallelism makes concrete, M18 matures the ecosystem so more standard programs
run, and M19 broadens device support as a parallel track. Multi-architecture
work stays deferred; these milestones must avoid spreading architecture-specific
assumptions into generic process, isolation, ecosystem, and device policy so a
later multi-architecture goal can build on them.

M16–M19 继续以 x86_64 为唯一交付目标，在单一架构上提升通用成熟度。它们直接构建在深度
阶段之上：M16 通过有界用户态并行让用户程序用满多核内核，M17 加固隔离与资源限制（其威胁
模型正由该并行能力变得具体），M18 完善生态以运行更多标准程序，M19 把设备支持作为并行
轨道拓宽。多架构工作仍延后；这些里程碑必须避免把 architecture-specific 假设扩散到通用
进程、隔离、生态与设备策略中，使后续多架构目标可在其上构建。

### Milestone M16 — User-Space Parallelism / 里程碑 M16 — 用户态并行

User-visible goal: a single user program can run multiple threads that the
kernel schedules across cores, so throughput within one program scales with core
count instead of being pinned to a single core.

用户可见目标：单个用户程序可以运行由内核跨核调度的多个线程，使单程序内吞吐随核数提升，
而不再被钉在单一核心上。

- [ ] Task M16.1: separate the bounded process model into a shared thread-group
  container and per-thread execution units, so per-thread state lives on the
  scheduler thread while address space, descriptors, working directory,
  identity, and signal dispositions stay shared, without changing single-threaded
  behavior.
- [ ] 任务 M16.1：将有界进程模型拆分为共享的线程组容器与每线程执行单元，使每线程状态归属
  调度线程，而地址空间、描述符、工作目录、身份与信号处置保持共享，且不改变单线程行为。
- [ ] Task M16.2: a bounded thread-creation syscall that adds a thread to the
  current address space with a caller-provided stack and thread-local storage,
  sharing the thread group's descriptors, working directory, and identity.
- [ ] 任务 M16.2：有界线程创建 syscall，在当前地址空间内新增线程，使用调用者提供的栈与
  thread-local 存储，并共享线程组的描述符、工作目录与身份。
- [ ] Task M16.3: a bounded fast user-space mutex primitive for wait and wake,
  reusing the existing scheduler wait queues and staying IRQ-safe.
- [ ] 任务 M16.3：有界的快速用户态互斥原语，支持等待与唤醒，复用既有调度等待队列并保持
  IRQ-safe。
- [ ] Task M16.4: thread-aware exit, wait, and signal semantics, including
  per-thread masks with shared dispositions, thread-directed versus
  group-directed pending signals, group termination on a fatal signal, and
  thread-group-scoped reaping, without claiming a complete threading model.
- [ ] 任务 M16.4：线程感知的 exit、wait 与 signal 语义，包括每线程屏蔽位配合共享处置、
  线程定向与线程组定向的 pending 信号、致命信号时的整组终止，以及以线程组为范围的回收，
  不声称完整线程模型。
- [ ] Task M16.5: a minimal user libc thread API providing thread create/join and
  a mutex built on the user-space mutex primitive, staying freestanding-safe.
- [ ] 任务 M16.5：最小用户 libc 线程 API，提供线程 create/join 与基于用户态互斥原语的
  mutex，保持 freestanding-safe。

### Milestone M17 — Isolation And Resource Limits / 里程碑 M17 — 隔离与资源限制

User-visible goal: the multi-threaded multi-process system stays trustworthy
under stress, so a single program cannot exhaust shared resources or breach the
kernel/user boundary.

用户可见目标：多线程多进程系统在压力下保持可信，使单个程序无法耗尽共享资源或突破
内核/用户边界。

- [ ] Task M17.1: enable architecture-supported kernel/user access protection so
  the kernel faults on stray user-pointer access except through audited copy
  paths, hardening the boundary that user-space parallelism widens.
- [ ] 任务 M17.1：启用架构支持的内核/用户访问保护，使内核在越过受审计拷贝路径的杂散
  用户指针访问时 fault，加固由用户态并行扩大的边界。
- [ ] Task M17.2: bounded per-process resource limits covering descriptors,
  memory, threads, and child processes, with deterministic enforcement and
  diagnostics.
- [ ] 任务 M17.2：有界的每进程资源限制，覆盖描述符、内存、线程与子进程，具备确定性
  强制与诊断。
- [ ] Task M17.3: a bounded read-only self-inspection surface exposing process,
  thread, memory, and descriptor state for diagnostics, without a broad procfs
  contract.
- [ ] 任务 M17.3：有界只读自省面，暴露进程、线程、内存与描述符状态用于诊断，
  不提供广义 procfs 契约。
- [ ] Task M17.4: a controlled shutdown and reset path behind an architecture glue
  interface, so the system can power off or restart deterministically.
- [ ] 任务 M17.4：受控关机与重启路径，置于架构 glue 接口之后，使系统可确定性关机或重启。
- [ ] Task M17.5: release-grade validation automation that runs the default-off
  smoke matrix reproducibly.
- [ ] 任务 M17.5：release 级验证自动化，可复现地运行默认关闭的 smoke 矩阵。

### Milestone M18 — Ecosystem Maturity / 里程碑 M18 — 生态成熟

User-visible goal: BigOS runs more standard small programs with fewer porting
obstacles, and can build a small program on itself.

用户可见目标：BigOS 以更少移植障碍运行更多标准小程序，并可在自身上构建一个小程序。

- [ ] Task M18.1: align signal, terminal, and libc behavior with the
  expectations of common small programs within bounded semantics.
- [ ] 任务 M18.1：在有界语义内，使 signal、终端与 libc 行为对齐常见小程序的预期。
- [ ] Task M18.2: port a set of real third-party small programs as conformance
  evidence, recording bounded gaps rather than claiming full compatibility.
- [ ] 任务 M18.2：移植一组真实第三方小程序作为符合性证据，记录有界差距而非声称完全兼容。
- [ ] Task M18.3: a bounded self-hosting path that compiles and runs a small
  program on BigOS, without claiming a full self-hosting toolchain.
- [ ] 任务 M18.3：有界自举路径，在 BigOS 上编译并运行一个小程序，不声称完整自举工具链。

### Milestone M19 — Broader Device Support / 里程碑 M19 — 更广设备支持

User-visible goal: BigOS drives more real hardware-style devices through the
existing device and async I/O framework, pursued as a parallel track that can
interleave with M16–M18.

用户可见目标：BigOS 通过既有设备与异步 I/O 框架驱动更多真实硬件风格设备，作为可与
M16–M18 穿插的并行轨道推进。

- [ ] Task M19.1: an additional modern storage driver such as AHCI or NVMe on the
  device and async I/O framework, validated through the emulator path.
- [ ] 任务 M19.1：基于设备与异步 I/O 框架的额外现代存储驱动，如 AHCI 或 NVMe，
  通过仿真器路径验证。
- [ ] Task M19.2: a bounded USB host and human-interface input path so keyboard
  and pointer input work beyond the legacy controller.
- [ ] 任务 M19.2：有界 USB host 与人机输入路径，使键盘与指针输入不再局限于 legacy
  控制器。
- [ ] Task M19.3: broaden the bounded network device path toward an additional
  real network backend within the existing interrupt-driven I/O boundaries.
- [ ] 任务 M19.3：在既有中断驱动 I/O 边界内，将有界网络设备路径拓宽到额外的真实网络后端。

### Parallel Foundations / 并行基础方向

- Backend and cleanup work may continue alongside the mainline, but the
  short-term plan does not add a new ISA.
- backend 与清理工作可与主线并行推进，但短期计划不接入新 ISA。
- All x86_64 work should avoid spreading architecture-specific assumptions into
  process, filesystem, userland ABI, and generic kernel policy.
- 所有 x86_64 工作都应避免把 architecture-specific 假设扩散到进程、文件系统、用户态 ABI 和通用
  内核策略中。

### Long-Term Goal / 长期目标

After the M16–M19 continuation phase, the deferred multi-architecture goal
carries BigOS toward the multi-architecture project goal. It is intentionally
kept at planning level and sequenced last so it builds on already-mature generic
capabilities rather than forcing them to be revalidated per ISA.

在 M16–M19 延续阶段之后，延后的多架构目标将 BigOS 推向多架构项目目标。它被有意保持在
规划层面，并排在最后，使其构建在已成熟的通用能力之上，而不必为每个 ISA 重复验证。

- Multi-architecture goal: formalize the architecture/core boundary by auditing
  the architecture-specific assumptions surfaced during the depth and
  continuation phases, then stand up a second ISA as a runnable backend to
  fulfill the multi-architecture project goal. This goal, not the short-term
  plan, is where a new ISA is introduced.
- 多架构目标：通过审查深度与延续阶段暴露出的 architecture-specific 假设来正式化
  architecture/core 边界，再把第二个 ISA 立为可运行 backend，以兑现多架构项目目标。
  新 ISA 在该目标中引入，而不在短期计划内。
