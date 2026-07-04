# BigOS Roadmap / BigOS 路线图

Language: English | 简体中文

This roadmap is the planning entry point for BigOS after the current incremental
POSIX-compatible userland baseline. It summarizes completed capabilities at a
high level and leaves clear space for future work. It does not replace detailed
architecture, implementation, validation, or change-tracking documents.

本文档是 BigOS 在当前增量 POSIX 兼容用户态基线之后的规划入口。它只在高层概述已完成能力，
并为后续工作留出清晰入口。它不替代详细架构、实现、验证或变更追踪文档。

Project goal: grow BigOS into a general-purpose, usable, POSIX-compatible
Unix-like operating system. The current runnable implementation remains
x86_64-only, now multi-core capable, with the x86_64 UEFI boot backend as the
default delivery baseline and the existing Legacy BIOS boot/storage path kept as
an explicit compatibility backend. Earlier bounded subsets remain useful as
incremental delivery checkpoints, not as permanent product limits.

项目目标：将 BigOS 推进为通用、可用、兼容 POSIX 的类 Unix 操作系统。当前可运行实现仍是
x86_64-only，现已具备多核能力，x86_64 UEFI boot backend 是默认交付基线，现有 Legacy BIOS
boot/storage 路径作为显式兼容 backend 保留。早期的有界子集仍作为增量交付检查点保留，
但不再作为长期产品能力上限。

## Current Implementation Summary / 当前实现概述

BigOS currently provides a smoke-tested, multi-core capable kernel with an
incremental POSIX-compatible userland. At a high level, the implemented baseline
includes:

BigOS 当前提供一个经过 smoke 验证、具备多核能力、具备增量 POSIX 兼容用户态的内核。从高层看，
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
- An incremental process and syscall layer with process lifecycle management,
  file-descriptor based I/O, anonymous demand paging, `fork`/COW, signals,
  time/identity primitives, image replacement, and per-process current-directory
  state.
- 增量进程与 syscall 层，包括进程生命周期管理、基于文件描述符的 I/O、匿名 demand
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

## Current Compatibility Discipline / 当前兼容纪律

BigOS is now planned as a general-purpose POSIX-compatible Unix-like system.
Future planning should treat compatibility as the long-term direction while
still delivering each milestone through explicit scope, deterministic errors,
capacity limits where needed, and reproducible validation. "Bounded" describes a
stage's implementation contract, not the final ambition of the project.

BigOS 现在按通用 POSIX 兼容类 Unix 系统规划。后续规划应把兼容性作为长期方向，同时仍要求
每个里程碑具备明确范围、确定性错误、必要的容量边界和可复现验证。“有界”描述的是阶段性
实现契约，而不是项目最终能力上限。

- Current runnable backends: x86_64 UEFI is the default boot
  backend; the existing Legacy BIOS style boot flow remains an explicit
  compatibility backend. Additional architectures are not yet runtime-parity
  backends.
- 当前可运行 backend：x86_64 UEFI 是默认启动 backend；现有 Legacy BIOS 风格
  启动流程仍作为显式兼容 backend 保留。其他架构尚不具备运行时等价能力。
- Execution model: multi-core capable with per-CPU scheduling and cross-CPU
  coordination, mostly synchronous I/O, and an incremental POSIX-compatible
  userland.
- 执行模型：具备多核能力，支持 per-CPU 调度与跨核协同，I/O 以同步为主，用户态按增量
  POSIX 兼容路径扩展。
- Userland: static programs, `/bin/sh`, libc wrappers, dynamic-link validation
  paths, terminal/process primitives, and packaged tools are present, but POSIX
  coverage is still incomplete and should be expanded in staged compatibility
  work.
- 用户态：已有静态程序、`/bin/sh`、libc wrapper、dynamic-link 验证路径、终端/进程原语和
  打包工具，但 POSIX 覆盖仍不完整，应在后续兼容性阶段继续扩展。
- Display: a single text-only console on the Legacy BIOS VGA text path with
  bounded scrollback; no graphical framebuffer rendering and no non-ASCII glyph
  rendering yet.
- 显示：仅有 Legacy BIOS VGA 文本路径上的单一文本控制台，具备有界 scrollback；尚无
  图形 framebuffer 渲染，也尚无非 ASCII 字形渲染。
- Source organization: kernel implementation and freestanding userland are
  separate top-level domains; future planning should preserve that boundary.
- 源码组织：内核实现与 freestanding 用户态是分离的顶层域；后续规划应保持这一边界。
- Memory/file model: anonymous demand paging/COW, writable runtime storage, and
  persistent clean-sync `/rw` storage are implemented. Broad file-backed `mmap`,
  POSIX filesystem completeness, journaling/crash recovery, async I/O, and
  broader storage/device support are compatibility-expansion work, not permanent
  non-goals.
- 内存/文件模型：已实现 anonymous demand paging/COW、可写运行期存储和 persistent clean-sync
  `/rw` 存储。广泛 file-backed `mmap`、完整 POSIX filesystem、journaling/crash recovery、
  async I/O 和更广存储/设备支持属于兼容性扩展工作，而非永久非目标。
- Boot/backends: the UEFI backend is the default x86_64 boot
  backend. Broader firmware parity, storage, device, and ISA backends remain
  future or parallel-track items.
- 启动/backend：UEFI backend 是默认 x86_64 启动 backend。更广泛的 firmware
  parity、storage、device、ISA backend 仍是后续或并行轨道事项。

## Completed Capability Baseline / 已完成能力基线

The kernel foundation, incremental userland, filesystem/path layer, and the M1–M5
mainline milestones are complete and now form a compressed minimal usable
system baseline. The completed work can be treated as the foundation for future
planning rather than as individual future-stage items.

内核基础、增量用户态、文件系统/路径层，以及 M1–M5 主线里程碑均已完成，并共同形成压缩后
的最小可用系统基线。后续规划应将这些工作视为基础能力，而不是继续把它们作为未来阶段
逐项展开。

- Kernel foundation: x86_64 legacy boot, text/serial output,
  interrupt/exception/syscall dispatch, timer and keyboard input, multi-core
  scheduling, kernel memory management, user address spaces, and bounded user
  fault handling.
- 内核基础：x86_64 legacy boot、文本/串口输出、中断/异常/syscall 分发、计时器与键盘输入、
  多核调度、内存管理、用户地址空间和有界用户 fault 处理。
- User-visible system baseline: resident init, interactive shell, static user
  programs, libc-style support, process lifecycle, `exec`/`fork`/COW,
  wait/exit behavior, signals, time and identity primitives, fd-based I/O,
  pipes, duplication, and redirection.
- 用户可见系统基线：常驻 init、交互式 shell、静态用户程序、libc 风格支持、进程生命周期、
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
future-planning items; their outcomes are now part of the current incremental
userland, filesystem, process, VM, multi-core, and persistent storage baseline.

已完成阶段已在上方“已完成能力基线”中压缩归纳。M1–M5 里程碑不应再作为独立的未来规划项
展开；其结果现在属于当前增量用户态、文件系统、进程、VM、多核与持久存储基线的一部分。

### Mainline Strategy / 主线策略

The next phase keeps x86_64 as the delivery target and grows user-visible
maturity in dependency order: first modernize the boot backend and display
subsystem, then consolidate the freshly landed multi-core base, then move I/O
toward asynchronous and interrupt-driven paths so modern storage and networking
can build on a correct foundation, then mature the userland, and then deepen the
architecture-neutral capabilities that make the system feel usable — user-visible
asynchronous I/O and multiplexing, a connection-oriented network stack, and a
recoverable writable filesystem — before broad compatibility expansion or
multi-architecture work. New code added along the way should respect the
existing boot, memory, interrupt, and multi-core boundaries so each milestone
stays a controlled extension rather than a broad retrofit. After the depth phase
the continuation phase stays on x86_64 and raises general-purpose POSIX/Unix
maturity in dependency order: first close filesystem and `mmap` compatibility
gaps that block many programs, then mature process, terminal, and job-control
semantics, then expand libc, shell, and core utility behavior, then add
user-space threading together with the resource limits that make concurrency
safe, then broaden socket/resolver compatibility, then use self-hosting and
third-party ports as conformance evidence. Broader device support remains a
parallel track. Multi-architecture work is deferred to a later long-term goal.

下一阶段保持 x86_64 为交付目标，并按依赖顺序推进用户可见成熟度：先现代化启动 backend
与显示子系统，再巩固刚落地的多核基线，随后将 I/O 推向异步与中断驱动路径，使现代存储与
网络能够构建在正确的基础之上，再完善用户态，然后在任何加固或多架构工作之前夯实让系统
真正可用的 architecture-neutral 能力——用户可见的异步 I/O 与多路复用、面向连接的网络栈，
以及可恢复的可写文件系统，再进入广泛兼容性扩展或多架构工作。沿途新增代码应尊重既有启动、
内存、中断与多核边界，使每个里程碑成为受控扩展，而非大范围 retrofit。深度阶段之后，延续
阶段继续保持 x86_64，并按依赖顺序提升通用 POSIX/Unix 成熟度：先补齐会阻塞大量程序的
filesystem 与 `mmap` 兼容缺口，再成熟 process、terminal 与 job-control 语义，然后扩展 libc、
shell 与核心工具行为，再把用户态线程与保障并发安全的资源限制一起引入，随后拓宽
socket/resolver 兼容性，最后用 self-hosting 与第三方程序移植作为符合性证据。更广设备支持
保持为并行轨道。多架构工作延后为长期目标。

The mainline extends with seventeen sequenced milestones (M6–M22) on top of the
completed capability baseline. Each milestone has a clear user-visible goal and
lists its tasks; they are pursued in M6–M22 order, where later milestones depend
on earlier ones. M6–M12 are complete. M13–M15 form the next depth phase: they
deepen architecture-neutral capabilities (user-visible asynchronous I/O and
multiplexing, a real networking stack, and a real writable filesystem) and are
validated once on x86_64 to reduce later multi-architecture rework. M16–M22 form
the x86_64 POSIX/Unix compatibility phase: they keep completed M1–M14 work as the
baseline while adding new milestones for the compatibility gaps those completed
stages intentionally left open. Multi-architecture work is deliberately deferred
to a later long-term goal so it builds on already-mature generic capabilities
rather than forcing them to be revalidated per ISA.

主线在已完成能力基线之上扩展为十七个有序里程碑（M6–M22）。每个里程碑都有清晰的用户可见
目标并列出其任务；它们按 M6–M22 的顺序推进，后续里程碑依赖在先的里程碑。M6–M12 已完成。
M13–M15 构成下一阶段的深度推进：先夯实 architecture-neutral 能力（用户可见的异步 I/O
与多路复用、真实网络栈、真实可写文件系统），并只在 x86_64 上验证一次，以降低后续多架构
的返工成本。M16–M22 构成 x86_64 POSIX/Unix 兼容性阶段：保留 M1–M14 已完成工作作为基线，
并通过新的里程碑补齐这些已完成阶段有意留下的兼容性缺口。多架构工作被有意延后为长期目标，
使其构建在已成熟的通用能力之上，而不必为每个 ISA 重复验证。

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

User-visible goal: BigOS boots by default through UEFI to the same userland,
while the Legacy BIOS path remains a runnable cross-validation backend.

用户可见目标：BigOS 默认通过 UEFI 启动并到达相同的用户态，同时 Legacy BIOS 路径仍
作为可运行的交叉验证 backend 保留。

- [x] Task M6.1: promote the UEFI backend from a staged spike to the default
  runnable boot backend, reaching the existing userland baseline.
- [x] 任务 M6.1：将 UEFI backend 从阶段性 spike 提升为默认可运行启动 backend，
  到达现有用户态基线。
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
interactive programs depend on, M14 grows the UDP network path into a usable
connection-oriented stack with DNS, and M15 turns the clean-sync `/rw` storage
into a recoverable writable filesystem. These milestones are incremental
compatibility steps toward a more complete Unix-like system, validated once on
the x86_64 delivery target while avoiding architecture-specific assumptions in
generic process, I/O, network, and filesystem policy.

M13–M15 在任何加固或多架构工作之前，先夯实 architecture-neutral 能力。它们直接构建在
已完成基线之上：M13 增加后续网络与交互式程序所依赖的用户可见 readiness 与多路复用模型，
M14 将 UDP 网络路径成长为带 DNS 的可用面向连接网络栈，M15 将 clean-sync `/rw` 存储升级为
可恢复的可写文件系统。这些里程碑是迈向更完整类 Unix 系统的增量兼容步骤，并只在 x86_64
交付目标上验证一次，同时必须避免把 architecture-specific 假设扩散到通用进程、I/O、网络
与文件系统策略中。

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
connect to a loopback address, and resolve names, on top of the existing network
path.

用户可见目标：用户程序可以在既有网络路径之上运行真实 TCP client/server 交互、连接
loopback 地址并解析名字。

- [x] Task M14.1: a loopback network path so connection-oriented and datagram
  traffic to the local address works without a physical or emulated network
  card, enabling reproducible default-off validation.
- [x] 任务 M14.1：loopback 网络路径，使面向连接与 datagram 的本机地址流量无需物理或仿真
  网卡即可工作，支撑可复现的默认关闭验证。
- [x] Task M14.2: a TCP path with connection setup, bounded retransmission and
  windowing, ordered delivery, and connection teardown, forming the first
  compatibility step toward a fuller TCP feature matrix.
- [x] 任务 M14.2：TCP 路径，包含连接建立、有界重传与窗口、有序交付与连接拆除，作为迈向
  更完整 TCP 特性矩阵的第一步兼容能力。
- [x] Task M14.3: a stream socket interface integrated with the fd, multiplexing,
  and syscall paths, exposing connect, listen, and accept within bounded
  semantics.
- [x] 任务 M14.3：与 fd、多路复用与 syscall 路径集成的 stream socket 接口，在有界语义内
  暴露 connect、listen 与 accept。
- [x] Task M14.4: a DNS client over the existing UDP path sufficient for basic
  name resolution, establishing the first resolver-compatible path before
  broader resolver and caching behavior.
- [x] 任务 M14.4：基于既有 UDP 路径的 DNS client，足以支持基础名字解析，并在后续扩展
  更完整 resolver 与 caching 行为之前建立第一版 resolver 兼容路径。

### Milestone M15 — Recoverable Writable Filesystem / 里程碑 M15 — 可恢复可写文件系统

User-visible goal: the filesystem stays consistent across a crash or power loss
and can mount more than one writable backend, moving beyond the current
clean-sync boundary.

用户可见目标：文件系统在崩溃或断电后仍保持一致，并可挂载多于一个可写后端，突破当前
clean-sync 边界。

  - [x] Task M15.1: a write-ahead journaling path for the writable `/rw` backend
  with bounded log records covering metadata and data ordering, establishing the
  first step toward stronger POSIX-style durability behavior.
  - [x] 任务 M15.1：可写 `/rw` 后端的 write-ahead journaling 路径，使用覆盖元数据与数据
  顺序的有界日志记录，作为迈向更强 POSIX-style 持久性行为的第一步。
- [x] Task M15.2: a mount-time recovery path that replays or discards the journal
  to restore a consistent filesystem state after an unclean shutdown.
- [x] 任务 M15.2：挂载时恢复路径，通过 replay 或丢弃 journal，在非干净关机后恢复一致的
  文件系统状态。
- [ ] Task M15.3: a bounded VFS mount framework that lets more than one writable
  filesystem backend attach at distinct mount points, preserving the read-only
  boot asset and existing `/rw` boundaries.
- [ ] 任务 M15.3：有界 VFS mount 框架，使多于一个可写文件系统后端可挂载在不同挂载点，
  同时保留只读启动资产与既有 `/rw` 边界。

### POSIX/Unix Compatibility Phase / POSIX/Unix 兼容性阶段

M16–M22 keep x86_64 as the only delivery target and use the completed M1–M14
capabilities as a baseline rather than reopening them. The phase adds new
milestones for compatibility gaps that matter to real Unix-style programs:
filesystem and `mmap` semantics, process and terminal behavior, libc/shell/tool
coverage, user-space threading and limits, socket/resolver behavior,
self-hosting evidence, and broader devices. Multi-architecture work stays
deferred; these milestones must avoid spreading architecture-specific assumptions
into generic filesystem, VM, process, userland ABI, networking, and device
policy so a later multi-architecture goal can build on them.

M16–M22 继续以 x86_64 为唯一交付目标，并把已完成的 M1–M14 能力作为基线而非重新打开。
该阶段通过新的里程碑补齐真实 Unix-style 程序会遇到的兼容性缺口：filesystem 与 `mmap`
语义、process 与 terminal 行为、libc/shell/tool 覆盖、用户态线程与资源限制、socket/resolver
行为、self-hosting 证据，以及更广设备支持。多架构工作仍延后；这些里程碑必须避免把
architecture-specific 假设扩散到通用 filesystem、VM、process、userland ABI、networking
与 device policy 中，使后续多架构目标可在其上构建。

### Milestone M16 — POSIX Filesystem And mmap Compatibility / 里程碑 M16 — POSIX 文件系统与 mmap 兼容

User-visible goal: programs see filesystem, path, metadata, and memory-mapping
behavior that is much closer to the POSIX expectations used by portable Unix
software.

用户可见目标：程序观察到更接近可移植 Unix 软件预期的 filesystem、path、metadata 与
memory-mapping 行为。

- [ ] Task M16.1: extend file-backed mapping beyond the current read-only path to
  cover private writable mappings, coherent page-cache interaction, and
  deterministic `munmap`/`mprotect` behavior over mapped file ranges.
- [ ] 任务 M16.1：将 file-backed mapping 从当前只读路径扩展到 private writable 映射、
  与 page cache 的一致性交互，以及 mapped file range 上确定性的 `munmap`/`mprotect` 行为。
- [ ] Task M16.2: define and implement staged `MAP_SHARED` writeback semantics for
  regular files, including dirty-page accounting, fsync/msync-style publication,
  and failure behavior when storage or capacity is exhausted.
- [ ] 任务 M16.2：定义并实现 regular file 的分阶段 `MAP_SHARED` 写回语义，覆盖 dirty page
  accounting、fsync/msync-style 发布，以及存储或容量耗尽时的失败行为。
- [ ] Task M16.3: mature rename, link, symlink, unlink, and directory mutation
  semantics so common POSIX path operations behave predictably across `/boot`,
  `/rw`, and mounted writable backends.
- [ ] 任务 M16.3：成熟 rename、link、symlink、unlink 与目录变更语义，使常见 POSIX path
  操作在 `/boot`、`/rw` 和已挂载可写后端上具备可预期行为。
- [ ] Task M16.4: expand file metadata and permission handling, including mode
  updates, ownership-visible fields, `umask`, timestamp precision policy, and a
  clearer inode/object identity contract.
- [ ] 任务 M16.4：扩展文件 metadata 与权限处理，包含 mode 更新、用户可见 ownership 字段、
  `umask`、时间戳精度策略，以及更清晰的 inode/object identity 契约。
- [ ] Task M16.5: validate filesystem and mapping compatibility with targeted
  userland probes that exercise real path, metadata, mmap, sync, and recovery
  combinations.
- [ ] 任务 M16.5：通过定向用户态探针验证 filesystem 与 mapping 兼容性，覆盖真实 path、
  metadata、mmap、sync 与 recovery 组合。

### Milestone M17 — POSIX Process, Terminal, And Job Control / 里程碑 M17 — POSIX 进程、终端与作业控制

User-visible goal: interactive programs and shells can rely on process groups,
sessions, terminal control, signals, and wait status behavior that match common
Unix expectations.

用户可见目标：交互式程序与 shell 可以依赖符合常见 Unix 预期的进程组、session、终端控制、
signal 与 wait status 行为。

- [ ] Task M17.1: mature process lifecycle and wait status reporting, including
  stopped/continued states, signal termination status, and `waitpid` selector
  behavior needed by shells and supervisors.
- [ ] 任务 M17.1：成熟进程生命周期与 wait status 报告，包含 stopped/continued 状态、
  signal termination status，以及 shell 和 supervisor 所需的 `waitpid` selector 行为。
- [ ] Task M17.2: complete foreground/background job-control behavior for the
  default terminal, including terminal-generated signals, background read/write
  policy, and shell-visible recovery paths.
- [ ] 任务 M17.2：补齐默认终端的前台/后台 job-control 行为，包含终端生成 signal、后台读写策略
  和 shell 可观察的恢复路径。
- [ ] Task M17.3: introduce a POSIX-facing `termios` subset over the existing
  terminal mode model, including canonical/raw behavior, echo flags, control
  characters, and deterministic unsupported-option handling.
- [ ] 任务 M17.3：在现有 terminal mode 模型之上引入面向 POSIX 的 `termios` 子集，覆盖
  canonical/raw 行为、echo flags、控制字符，以及 unsupported option 的确定性处理。
- [ ] Task M17.4: add a pseudo-terminal or multi-terminal foundation sufficient
  for shell tests, simple terminal-aware programs, and future remote/session
  workflows.
- [ ] 任务 M17.4：增加 pseudo-terminal 或 multi-terminal 基础，足以支撑 shell 测试、简单
  terminal-aware 程序，以及未来 remote/session 工作流。
- [ ] Task M17.5: align signal delivery across process groups, terminal events,
  and exec/wait boundaries without regressing existing single-process signal
  behavior.
- [ ] 任务 M17.5：对齐 process group、terminal event 与 exec/wait 边界上的 signal 投递，
  同时不回退既有单进程 signal 行为。

### Milestone M18 — POSIX Libc, Shell, And Core Utilities / 里程碑 M18 — POSIX libc、Shell 与核心工具

User-visible goal: common small Unix programs build and run with fewer BigOS
patches because libc headers, shell behavior, and packaged tools cover the
expected compatibility surface.

用户可见目标：常见小型 Unix 程序能以更少 BigOS patch 构建和运行，因为 libc header、shell
行为与打包工具覆盖了预期兼容面。

- [ ] Task M18.1: expand libc headers and wrappers for the POSIX APIs most often
  required by small portable programs, keeping errno, type widths, and feature
  macros consistent.
- [ ] 任务 M18.1：扩展小型可移植程序最常需要的 POSIX API 对应 libc header 与 wrapper，
  保持 errno、类型宽度和 feature macro 一致。
- [ ] Task M18.2: mature stdio, directory traversal, environment, string/locale
  stubs, time, and process wrappers so source packages need fewer compatibility
  shims.
- [ ] 任务 M18.2：成熟 stdio、目录遍历、environment、string/locale stub、time 与 process
  wrapper，使源码包需要更少兼容 shim。
- [ ] Task M18.3: broaden `/bin/sh` grammar and execution behavior, including
  multi-stage pipelines, quoting, variable expansion, command status propagation,
  and script execution for small build/test scripts.
- [ ] 任务 M18.3：拓宽 `/bin/sh` grammar 与执行行为，包含多级 pipeline、quoting、变量展开、
  命令状态传播，以及小型 build/test script 执行。
- [ ] Task M18.4: align packaged core utilities with common POSIX behavior for
  options, stdin/stdout/stderr handling, exit status, diagnostics, and pipeline
  composition.
- [ ] 任务 M18.4：将打包核心工具在 option、stdin/stdout/stderr、退出状态、诊断与 pipeline
  组合方面对齐常见 POSIX 行为。
- [ ] Task M18.5: mature the dynamic-link path toward ordinary shared-library
  execution, including loader diagnostics, relocation coverage, and stable
  interpreter/user ABI.
- [ ] 任务 M18.5：将 dynamic-link 路径推进到普通 shared-library 执行，覆盖 loader 诊断、
  relocation 范围和稳定 interpreter/user ABI。

### Milestone M19 — User-Space Threads, Futex, And Resource Limits / 里程碑 M19 — 用户态线程、Futex 与资源限制

User-visible goal: a single user program can run multiple threads across cores
while the kernel enforces resource limits and preserves process, signal, and
memory safety under concurrency.

用户可见目标：单个用户程序可以跨核运行多个线程，同时内核在并发下强制资源限制，并保持
process、signal 与 memory safety。

- [ ] Task M19.1: separate the current process model into a shared thread-group
  container and per-thread execution units while preserving single-threaded
  behavior.
- [ ] 任务 M19.1：将当前进程模型拆分为共享线程组容器与每线程执行单元，同时保持单线程行为不变。
- [ ] Task M19.2: add thread creation, join, exit, TLS setup, and libc thread
  wrappers sufficient for a staged pthread-compatible subset.
- [ ] 任务 M19.2：增加 thread create、join、exit、TLS setup 与 libc thread wrapper，支撑分阶段
  pthread-compatible 子集。
- [ ] Task M19.3: provide a futex-style wait/wake primitive for user-space mutexes,
  condition variables, and runtime synchronization.
- [ ] 任务 M19.3：提供 futex-style wait/wake primitive，用于用户态 mutex、condition variable
  与 runtime synchronization。
- [ ] Task M19.4: enforce per-process and per-thread resource limits covering
  descriptors, memory, threads, child processes, and pending synchronization
  waits.
- [ ] 任务 M19.4：强制每进程与每线程资源限制，覆盖 descriptor、memory、thread、child process
  与 pending synchronization wait。
- [ ] Task M19.5: enable architecture-supported kernel/user access protection and
  audit copy paths so concurrent user programs cannot widen unsafe pointer
  access.
- [ ] 任务 M19.5：启用架构支持的 kernel/user 访问保护并审计 copy path，避免并发用户程序扩大
  不安全用户指针访问面。

### Milestone M20 — Socket, Resolver, And Network Compatibility / 里程碑 M20 — Socket、Resolver 与网络兼容

User-visible goal: portable network programs can use the socket and resolver
APIs they expect for IPv4 TCP/UDP workflows, with clear staged paths for IPv6
and broader networking.

用户可见目标：可移植网络程序可以使用其预期的 socket 与 resolver API 完成 IPv4 TCP/UDP 工作流，
并为 IPv6 与更广网络能力保留清晰的分阶段路径。

- [ ] Task M20.1: expand socket operations with `shutdown`, `getsockname`,
  `getpeername`, `setsockopt`, `SO_REUSEADDR`, and other options required by
  common client/server programs.
- [ ] 任务 M20.1：扩展 socket 操作，加入 `shutdown`、`getsockname`、`getpeername`、
  `setsockopt`、`SO_REUSEADDR` 以及常见 client/server 程序需要的其它 option。
- [ ] Task M20.2: add `sendmsg`/`recvmsg` or staged equivalents for scatter-gather
  and message-oriented compatibility while preserving existing stream/datagram
  behavior.
- [ ] 任务 M20.2：增加 `sendmsg`/`recvmsg` 或分阶段等价能力，支撑 scatter-gather 与消息型兼容，
  同时保持既有 stream/datagram 行为。
- [ ] Task M20.3: mature resolver and `netdb.h` behavior, including `getaddrinfo`,
  service lookup, DNS search/config policy, and deterministic cache or no-cache
  semantics.
- [ ] 任务 M20.3：成熟 resolver 与 `netdb.h` 行为，包括 `getaddrinfo`、service lookup、
  DNS search/config 策略，以及确定性的 cache 或 no-cache 语义。
- [ ] Task M20.4: broaden TCP behavior where required by real programs, including
  keepalive, linger policy, connection reset/error reporting, and compatibility
  diagnostics.
- [ ] 任务 M20.4：按真实程序需要拓宽 TCP 行为，包括 keepalive、linger policy、连接 reset/error
  报告和兼容性诊断。
- [ ] Task M20.5: define the IPv6 and multi-interface expansion boundary, even if
  initial implementation remains focused on IPv4 and the current network backend.
- [ ] 任务 M20.5：定义 IPv6 与多接口扩展边界，即使初始实现仍聚焦 IPv4 与当前网络后端。

### Milestone M21 — Self-Hosting And Third-Party Program Compatibility / 里程碑 M21 — 自举与第三方程序兼容

User-visible goal: BigOS proves compatibility by building and running real
third-party programs, then uses their failures to drive the next compatibility
work.

用户可见目标：BigOS 通过构建和运行真实第三方程序证明兼容性，并用这些程序的失败点驱动后续
兼容工作。

- [ ] Task M21.1: select and maintain a small conformance program set covering
  shell scripts, libc-heavy tools, filesystem users, terminal-aware programs, and
  network clients/servers.
- [ ] 任务 M21.1：选择并维护一组小型符合性程序集合，覆盖 shell script、libc-heavy 工具、
  filesystem 用户、terminal-aware 程序和网络 client/server。
- [ ] Task M21.2: bring up an initial on-BigOS compile-and-run path for a small C
  program, including assembler/linker/runtime assumptions and filesystem
  workspace behavior.
- [ ] 任务 M21.2：打通在 BigOS 上编译并运行小型 C 程序的初始路径，覆盖 assembler/linker/runtime
  假设与 filesystem workspace 行为。
- [ ] Task M21.3: create a compatibility-gap tracking format that ties each porting
  failure to a syscall, libc, filesystem, terminal, network, or tooling gap.
- [ ] 任务 M21.3：建立兼容性缺口跟踪格式，将每个移植失败点关联到 syscall、libc、filesystem、
  terminal、network 或 tooling 缺口。
- [ ] Task M21.4: establish reproducible source-package build validation for the
  selected program set, including expected skips and environment blockers.
- [ ] 任务 M21.4：为选定程序集合建立可复现 source-package build validation，包含预期 skip
  与环境 blocker。
- [ ] Task M21.5: use the conformance evidence to update later roadmap priorities
  rather than treating self-hosting as a one-off demo.
- [ ] 任务 M21.5：使用符合性证据更新后续 roadmap 优先级，而不是把 self-hosting 作为一次性 demo。

### Milestone M22 — Broader Device Support / 里程碑 M22 — 更广设备支持

User-visible goal: BigOS drives more real hardware-style devices through the
existing device and async I/O framework, pursued as a parallel track that can
interleave with M16–M21.

用户可见目标：BigOS 通过既有设备与异步 I/O 框架驱动更多真实硬件风格设备，作为可与
M16–M21 穿插的并行轨道推进。

- [ ] Task M22.1: an additional modern storage driver such as AHCI or NVMe on the
  device and async I/O framework, validated through the emulator path.
- [ ] 任务 M22.1：基于设备与异步 I/O 框架的额外现代存储驱动，如 AHCI 或 NVMe，
  通过仿真器路径验证。
- [ ] Task M22.2: a USB host and human-interface input path so keyboard and
  pointer input work beyond the legacy controller.
- [ ] 任务 M22.2：USB host 与人机输入路径，使键盘与指针输入不再局限于 legacy 控制器。
- [ ] Task M22.3: broaden the network device path toward an additional real
  network backend within the existing interrupt-driven I/O boundaries.
- [ ] 任务 M22.3：在既有中断驱动 I/O 边界内，将网络设备路径拓宽到额外的真实网络后端。
- [ ] Task M22.4: add device discovery and diagnostic surfaces needed to debug
  storage, USB, and network backends without committing to a full device manager.
- [ ] 任务 M22.4：增加调试 storage、USB 与 network backend 所需的设备发现和诊断面，但不承诺
  完整 device manager。

### Parallel Foundations / 并行基础方向

- Backend and cleanup work may continue alongside the mainline, but the
  short-term plan does not add a new ISA.
- backend 与清理工作可与主线并行推进，但短期计划不接入新 ISA。
- All x86_64 work should avoid spreading architecture-specific assumptions into
  process, filesystem, userland ABI, and generic kernel policy.
- 所有 x86_64 工作都应避免把 architecture-specific 假设扩散到进程、文件系统、用户态 ABI 和通用
  内核策略中。

### Long-Term Goal / 长期目标

After the M16–M22 POSIX/Unix compatibility phase, the deferred multi-architecture goal
carries BigOS toward the multi-architecture project goal. It is intentionally
kept at planning level and sequenced last so it builds on already-mature generic
capabilities rather than forcing them to be revalidated per ISA.

在 M16–M22 POSIX/Unix 兼容性阶段之后，延后的多架构目标将 BigOS 推向多架构项目目标。它被有意保持在
规划层面，并排在最后，使其构建在已成熟的通用能力之上，而不必为每个 ISA 重复验证。

- Multi-architecture goal: formalize the architecture/core boundary by auditing
  the architecture-specific assumptions surfaced during the depth and
  continuation phases, then stand up a second ISA as a runnable backend to
  fulfill the multi-architecture project goal. This goal, not the short-term
  plan, is where a new ISA is introduced.
- 多架构目标：通过审查深度与兼容性阶段暴露出的 architecture-specific 假设来正式化
  architecture/core 边界，再把第二个 ISA 立为可运行 backend，以兑现多架构项目目标。
  新 ISA 在该目标中引入，而不在短期计划内。
