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
can build on a correct foundation, and finally mature the userland. New code
added along the way should respect the existing boot, memory, interrupt, and
multi-core boundaries so each milestone stays a controlled extension rather than
a broad retrofit.

下一阶段保持 x86_64 为交付目标，并按依赖顺序推进用户可见成熟度：先现代化启动 backend
与显示子系统，再巩固刚落地的多核基线，随后将 I/O 推向异步与中断驱动路径，使现代存储与
网络能够构建在正确的基础之上，最后完善用户态。沿途新增代码应尊重既有启动、内存、中断
与多核边界，使每个里程碑成为受控扩展，而非大范围 retrofit。

The mainline extends with seven sequenced milestones (M6–M12) on top of the
completed capability baseline. Each milestone has a clear user-visible goal and
lists its tasks; they are pursued in M6–M12 order, where later milestones depend
on earlier ones.

主线在已完成能力基线之上扩展为七个有序里程碑（M6–M12）。每个里程碑都有清晰的用户可见
目标并列出其任务；它们按 M6–M12 的顺序推进，后续里程碑依赖在先的里程碑。

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

- [ ] Task M11.1: a network device driver such as virtio-net on the device and
  interrupt-driven I/O framework.
- [ ] 任务 M11.1：基于设备与中断驱动 I/O 框架的网络设备驱动，如 virtio-net。
- [ ] Task M11.2: a bounded network protocol path sufficient for basic
  communication, without claiming a complete network stack.
- [ ] 任务 M11.2：足以支撑基础通信的有界网络协议路径，不声称完整网络栈。
- [ ] Task M11.3: a minimal user-visible socket interface integrated with the
  existing fd/syscall path within bounded semantics.
- [ ] 任务 M11.3：在有界语义内与既有 fd/syscall 路径集成的最小用户可见 socket 接口。

### Milestone M12 — Userland Maturity / 里程碑 M12 — 用户态成熟度

User-visible goal: BigOS feels more like a usable system, with more standard
programs and a richer userland.

用户可见目标：BigOS 更像一个可用系统，支持更多标准程序与更丰富的用户态。

- [ ] Task M12.1: dynamic linking and shared library support within bounded
  freestanding-safe semantics.
- [ ] 任务 M12.1：在有界 freestanding-safe 语义内支持动态链接与共享库。
- [ ] Task M12.2: a more complete libc subset toward portable standard small
  programs.
- [ ] 任务 M12.2：更完整的 libc 子集，支撑可移植的标准小程序。
- [ ] Task M12.3: a broader set of bounded core userland utilities.
- [ ] 任务 M12.3：更广的一组有界核心用户态工具程序。

### Parallel Foundations / 并行基础方向

- Backend and cleanup work may continue alongside the mainline, but the
  short-term plan does not add a new ISA.
- backend 与清理工作可与主线并行推进，但短期计划不接入新 ISA。
- All x86_64 work should avoid spreading architecture-specific assumptions into
  process, filesystem, userland ABI, and generic kernel policy.
- 所有 x86_64 工作都应避免把 architecture-specific 假设扩散到进程、文件系统、用户态 ABI 和通用
  内核策略中。
