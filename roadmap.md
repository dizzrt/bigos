# BigOS Roadmap / BigOS 路线图

Language: English | 简体中文

This roadmap summarizes the completed baseline and sets the recommended
direction toward a general-purpose, POSIX-compatible, multi-architecture kernel.
It is a planning document, not a replacement for `docs/`, `openspec/specs/`, or
archived OpenSpec changes.

本文档压缩整理已完成基线，并给出面向「通用、兼容 POSIX、支持多架构的内核」目标的
后续阶段建议。它是规划文档，不替代 `docs/`、`openspec/specs/` 或已归档的
OpenSpec change。

Project goals (decided): a general-purpose POSIX-compatible kernel that will grow
an architecture-abstraction layer to support multiple ISAs. The current code is
x86_64-only; multi-arch support is a planned future direction, not a current
capability.

项目目标（已确定）：一个通用、兼容 POSIX 的内核，并将逐步完善架构抽象层以支持多种
ISA。当前代码仅支持 x86_64；多架构支持是规划中的后续方向，而非现有能力。

## Completed Baseline / 已完成基线

BigOS has completed early bring-up: a smoke-tested single-core kernel with a
minimal, mostly synchronous user-mode path.

BigOS 已完成早期 bring-up：一个带 smoke 验证、以同步为主的单核内核与最小用户态
路径。

- Boot: Legacy BIOS/MBR/exFAT boot sectors load `/boot/boot.bin`, then the
  higher-half ELF64 `kernel`.
- 引导：Legacy BIOS/MBR/exFAT 引导扇区加载 `/boot/boot.bin`，再加载高半区
  ELF64 `kernel`。
- Runtime core: VGA/COM1 output, kernel-owned IDT, exception/IRQ/syscall
  dispatch, i8259 PIC, PIT IRQ0 tick, keyboard IRQ1 to TTY/console, a single-core
  scheduler with time-slice/preemption-disable semantics, explicit blocking
  primitives, and `int 0x80` dispatch.
- 运行时核心：VGA/COM1 输出、内核自有 IDT、exception/IRQ/syscall 分发、i8259、
  PIT IRQ0 tick、键盘 IRQ1 到 TTY/console、带时间片与 preemption-disable 的单核
  调度器、显式阻塞原语、`int 0x80` 分发。
- Blocking model: thread wait states, wait queues, wake-one/wake-all, timeout
  waits, blocking-context guards, timer-backed sleep.
- 阻塞模型：线程等待状态、wait queue、wake-one/wake-all、timeout wait、阻塞
  上下文保护、timer-backed sleep。
- Memory: buddy, slab/kmalloc, kernel virtual memory, direct map, user
  address-space derivation/teardown, owned empty PT/PD/PDPT reclamation, VMA
  tracking, `brk`, restricted anonymous mappings, fault-driven stack growth, and
  VMA-backed user range validation.
- 内存：buddy、slab/kmalloc、内核虚拟内存、direct map、用户地址空间派生/teardown、
  空 PT/PD/PDPT 回收、VMA 跟踪、`brk`、受限匿名映射、按 fault 的用户栈增长、基于
  VMA 的用户地址范围验证。
- Storage/FS: synchronous read-only ATA PIO, MBR exFAT discovery, read-only
  exFAT mount/path lookup, bounded file reads, a minimal read-only fd/VFS shell,
  and `open`/`read`/`close`.
- 存储/文件系统：同步只读 ATA PIO、MBR exFAT 发现、只读 exFAT mount/path lookup、
  bounded file read、最小只读 fd/VFS 壳层、`open`/`read`/`close`。
- User mode: a normal process-lifecycle core (bounded PID allocation, static
  process table, parent/child linkage, `wait`/`exit`, safe zombie/reaper
  teardown, a bounded ELF64 `exec` path with basic `argv`/`envp`). The
  first-user-program and filesystem-backed ELF64 paths remain default-off
  consumers behind `user_program_smoke` and `user_elf_smoke`.
- 用户态：进程生命周期核心（bounded PID 分配、静态进程表、父子关系、
  `wait`/`exit`、安全 zombie/reaper teardown、带基础 `argv`/`envp` 的 bounded
  ELF64 `exec`）。首用户程序与文件系统 ELF64 路径仍是默认关闭的
  `user_program_smoke` / `user_elf_smoke` 消费者。
- Build/Validation: `xmake` is the primary build; smoke options via
  `xmake f ...=y`; QEMU/Bochs via `xmake run` and `tools/boot_debug.py`; the
  stage 9 runtime smoke matrix is productized with QEMU headless serial-marker
  checks, per-case timeouts, structured artifacts, and explicit skip reasons.
- 构建/验证：`xmake` 为主构建；smoke 选项用 `xmake f ...=y`；QEMU/Bochs 经
  `xmake run` 与 `tools/boot_debug.py`；阶段 9 runtime smoke 矩阵已产品化（headless
  串口 marker、按 case timeout、结构化 artifact、明确跳过原因）。

## Current Boundary / 当前边界

BigOS is a controlled research kernel, not yet a general-purpose OS. It is
single-core, synchronous, read-only-FS, no-fork, and no-signal, with no
user-space libc. Crucially, normal boot never enters ring3: the only
`run_user_process` call sites sit behind default-off smoke switches.

BigOS 是受控研究内核，尚非通用 OS。它是单核、同步、只读文件系统、无 fork、
无信号，且无用户态 libc。关键事实：normal boot 从不进入 ring3——唯一调用
`run_user_process` 的位置都在默认关闭的 smoke 开关之后。

```text
Normal boot today / 当前 normal boot
  BIOS -> higher-half kernel -> mm/runtime init -> IRQ/timer/TTY
       -> scheduler + preemption -> blocking/sleep -> int 0x80 (10 syscalls)
       -> proc::init() -> sched::start() -> idle hlt
          (user-mode entry exists ONLY behind user_program_smoke / user_elf_smoke)

Missing general / POSIX layer / 尚缺的通用 / POSIX 层
  unify errno -> default-on init -> demand paging -> growable tables
       -> fork/COW -> time & identity -> signals
       -> writable FS + page cache + pipe -> userland/libc/shell
  (parallel: arch abstraction · behavior-assertion testing)
```

## Recommended Stages / 推荐阶段

The completed work above already decided two things kept below: introduce demand
paging only after fault recovery / allocation-failure / process-kill semantics
are designed, and plan COW together with `fork`. Detailed history lives in the
archived OpenSpec changes under `openspec/changes/archive/`.

上述已完成工作已确立两条后续仍沿用的判断：在设计好 fault recovery、分配失败与
进程 kill 语义后再引入 demand paging；将 COW 与 `fork` 一起规划。详细历史见
`openspec/changes/archive/` 下已归档的 OpenSpec change。

### Stage 14.1: Unify errno / 阶段 14.1：统一 errno

Status: completed (archived as `2026-06-09-unify-errno`). Independent; done before any other stage.

状态：已完成（已归档为 `2026-06-09-unify-errno`）。独立，已先于其他所有阶段完成。

Goal: converge the per-subsystem error codes into a single source of truth.

目标：把各子系统的错误码收敛到单一来源。

- Error codes are currently defined per-subsystem with duplicate values
  (`SYS_EBADF` vs `FD_EBADF`, `SYS_EWOULDBLOCK` vs `WAIT_EWOULDBLOCK`, etc.).
  Collapse them into one `bigos/errno.h`.
- 错误码目前按子系统各自定义且值重复（`SYS_EBADF` 与 `FD_EBADF`、
  `SYS_EWOULDBLOCK` 与 `WAIT_EWOULDBLOCK` 等）。收敛到单一 `bigos/errno.h`。
- Pure mechanical convergence, zero semantic risk; cheap now while only ~10
  values exist, a nightmare once POSIX spreads dozens of codes.
- 纯机械收敛、零语义风险；趁现在仅约 10 个值时做成本极低，POSIX 铺开几十个码后
  再做则是噩梦。

### Stage 14.5: Default-On User-Space Init / 阶段 14.5：默认进入用户态 init

Status: completed (archived as `2026-06-09-default-on-user-space-init`). Prerequisite and runway for all POSIX work below.

状态：已完成（已归档为 `2026-06-09-default-on-user-space-init`）。是下方所有 POSIX 工作的前置起跑线。

Goal: make normal boot enter ring3 by default, so process/exec/VFS/ELF
capabilities become a continuously exercised default path instead of smoke-only.

目标：让 normal boot 默认进入 ring3，使进程/exec/VFS/ELF 能力成为默认路径上被
持续验证的行为，而不是仅 smoke。

- Add a default-on `launch_init()` between `proc::init()` and `sched::start()`:
  reuse `vfs::init` -> read `/boot/user/init.elf` -> `create_elf_user_process`
  -> `run_user_process`.
- 在 `proc::init()` 与 `sched::start()` 之间新增默认开启的 `launch_init()`：复用
  现有 VFS/ELF 路径，从 `#ifdef` 中解放出来。
- Keep `user_program_smoke` / `user_elf_smoke` as extra validation switches; do
  not delete the existing matrix.
- 保留 `user_program_smoke` / `user_elf_smoke` 作为额外验证开关，不删除已有矩阵。
- Define deterministic degradation: missing/invalid `init.elf` -> panic with a
  `BIGOS_INIT_*` marker (PID-1 semantics). Define kernel behavior when init exits
  or is reaped.
- 定义确定性降级：init 缺失或非法 -> 带 `BIGOS_INIT_*` marker 的 panic（PID 1
  语义雏形）；定义 init 退出/被 reap 后内核的行为约定。
- Validation: assert `BIGOS_INIT_ENTER`/`BIGOS_INIT_EXIT` in the default build
  with no smoke flags; add to the stage 9 matrix as a default case; source
  contract asserts `launch_init` has no `#ifdef` guard.
- 验证：在不加任何 smoke 开关的默认构建中断言 init marker，纳入阶段 9 矩阵默认
  case，源码契约断言 `launch_init` 无 `#ifdef` 守卫。
- Test upgrade (enabled here): now that normal boot runs a real user program,
  start shifting validation from source-contract string asserts toward behavior
  assertions (serial markers + user-binary output). This kicks off the
  Behavior-Assertion Testing track below; treat it as ongoing, not a one-shot.
- 测试升级（在此启用）：normal boot 已能运行真实用户程序，开始把验证从源码字符串
  断言转向行为断言（serial marker + 用户态二进制输出）。这启动了下方「行为断言测试」
  轨道；视为持续推进，而非一次性。

### Stage 15: Demand Paging & Page-Fault Policy / 阶段 15：按需分页与缺页策略

Status: proposed. First POSIX foundation; prerequisite for fork/COW/mmap.

状态：建议中。第一块 POSIX 地基，是 fork/COW/mmap 的前置。

Goal: generalize the existing `try_handle_current_stack_fault` into a unified
page-fault handler.

目标：把现有 `try_handle_current_stack_fault` 泛化为统一缺页处理。

- Lazy anonymous-page materialization, allocation-failure -> deterministic
  process kill, kernel-mode faults still panic.
- 匿名页惰性分配、分配失败 -> 确定性进程 kill、内核态 fault 仍 panic。
- Do this standalone and validate independently before fork/COW.
- 单独完成并独立验证，再进入 fork/COW。

### Stage 15.5: Growable Process/Fd Tables / 阶段 15.5：进程与 fd 表可增长

Status: proposed. Hard prerequisite for Stage 16.

状态：建议中。是阶段 16 的硬前置。

Goal: remove the static-slot ceiling before `fork` can hit it.

目标：在 `fork` 撞上静态槽位上限之前移除它。

- Evolve the static process and fd tables (`MAX_PROCESSES=16`, `MAX_FDS=16`) into
  growable/recyclable structures with proper PID allocation/reclamation.
- 把静态进程表与 fd 表（`MAX_PROCESSES=16`、`MAX_FDS=16`）改为可增长/可回收结构，
  并配套 PID 的分配与回收。
- `MAX_VMAS`, `EXEC_MAX_ARGC`, and other compile-time ceilings can follow later,
  driven by real need rather than upfront.
- `MAX_VMAS`、`EXEC_MAX_ARGC` 等其他编译期上限可稍后按真实需求跟进，不必提前做。

### Stage 16: `fork` + Copy-On-Write / 阶段 16：`fork` 与写时复制

Status: proposed. Depends on Stage 15 and Stage 15.5.

状态：建议中。依赖阶段 15 与阶段 15.5。

Goal: real process duplication, enabling `fork`+`exec` and a future shell.

目标：真正的进程复制，使 `fork`+`exec` 与未来 shell 成为可能。

- COW address-space copy, page refcounts, and write-time split.
- COW 地址空间复制、页引用计数、写时分裂。
- Assumes growable process/fd tables from Stage 15.5 are already in place.
- 假定阶段 15.5 的可增长进程/fd 表已就位。

### Stage 16.5: Time & Identity / 阶段 16.5：时间与身份

Status: proposed. Prerequisite for Stages 17 and 18.

状态：建议中。是阶段 17 与 18 的前置。

Goal: add the wall-clock and identity primitives that signals and writable FS
both depend on.

目标：补齐信号与可写文件系统都依赖的墙钟与身份原语。

- Add a wall-clock/RTC source alongside the existing monotonic tick.
- 在现有 monotonic tick 之外，新增墙钟/RTC 来源。
- Introduce uid/gid and a basic permission model (who may signal whom; file
  owner/mode for Stage 18).
- 引入 uid/gid 与基础权限模型（谁能 kill 谁；阶段 18 的文件 owner/mode）。
- Keep it minimal and driven by the two consumers below; no broad POSIX user/
  group database yet.
- 保持最小并由下方两个消费者驱动；暂不做完整的 POSIX 用户/组数据库。

### Stage 17: Signals / 阶段 17：信号子系统

Status: proposed. Depends on Stage 16 and Stage 16.5.

状态：建议中。依赖阶段 16 与阶段 16.5。

Goal: a minimal POSIX signal model.

目标：最小 POSIX 信号模型。

- Per-process signal queue, default actions, `kill`/`sigaction`/masks, and
  delivery on IRQ-return (reuse the existing reschedule-on-IRQ-return hook).
- 进程信号队列、默认动作、`kill`/`sigaction`/掩码、IRQ-return 时投递（复用现有
  reschedule-on-IRQ-return 钩子）。
- Consumes the wall-clock and uid/gid primitives from Stage 16.5 (who may signal
  whom).
- 消费阶段 16.5 的墙钟与 uid/gid 原语（谁能 kill 谁）。
- Arch note: signal frame setup and IRQ-return delivery are arch-coupled; route
  them through the arch trap-frame interface (see Architecture Abstraction) rather
  than hard-coding x86 mechanisms, so the model stays portable.
- 架构注意：信号帧构造与 IRQ-return 投递是架构耦合的；应通过 arch trap-frame 接口
  （见「架构抽象」）走，而非硬编码 x86 机制，以保持模型可移植。

### Stage 18: Writable FS + Page Cache + Pipe / 阶段 18：可写文件系统 + 页缓存 + 管道

Status: proposed. Depends on Stage 15 and Stage 16.5.

状态：建议中。依赖阶段 15 与阶段 16.5。

Goal: general-purpose I/O semantics.

目标：通用 I/O 语义。

- Page/buffer cache first, then exFAT write or a simpler FS (e.g. ext2);
  `pipe`/`dup` to unlock shell pipelines.
- 先 page/buffer cache，再做 exFAT 写或更简单的 FS（如 ext2）；`pipe`/`dup` 解锁
  shell 管道。
- Builds on the uid/gid + permission model (owner/mode) from Stage 16.5; do not
  bolt permissions on afterward.
- 建立在阶段 16.5 的 uid/gid 与权限模型（owner/mode）之上；不要事后再补权限。

### Stage 19: Userland Runtime / libc / Shell / 阶段 19：用户态运行时 / libc / shell

Status: proposed (the former `plan-userland-runtime`). Consumes Stages 15-18.

状态：建议中（原 `plan-userland-runtime`）。消费阶段 15-18。

Goal: the smallest usable userland.

目标：最小可用用户态。

- Minimal crt0, syscall wrappers, `/bin/sh`, and user test binaries.
- 最小 crt0、syscall wrapper、`/bin/sh`、用户态测试二进制。
- Place last because it builds on all the semantics above.
- 放最后，因为它建立在上述全部语义之上。

## Continuous Concerns / 持续性关注

Cross-cutting items with no single completion point. The bounded debts that did
have a clear time point have been promoted into the staged sequence above
(Stage 14.1 errno, Stage 15.5 growable tables, Stage 16.5 time & identity). The
two below stay here because they advance continuously rather than finishing once.

无单一完成点的横切事项。原本有明确时间点的有界债务已提升为上方的阶段（14.1
errno、15.5 可增长表、16.5 时间与身份）。下面两项留在此处，因为它们持续推进、
不会一次完成。

### Behavior-Assertion Testing / 行为断言测试

- Current source-contract tests assert raw C++ source strings (e.g.
  `test_user_elf_program_loader_source.py`), which break on any equivalent
  refactor and cannot validate fork/COW/page-cache behavior. Kicked off by Stage
  14.5, progressively shift validation toward behavior assertions (serial markers
  + user-binary output) as a stage-9 matrix evolution, growing with each new
  POSIX stage.
- 现有 source-contract 测试断言原始 C++ 源码字符串（如
  `test_user_elf_program_loader_source.py`），重构即误报，且无法验证
  fork/COW/page cache 行为。由阶段 14.5 启动，逐步把验证转向行为断言（serial
  marker + 用户态二进制输出），作为阶段 9 矩阵的演进，随每个新 POSIX 阶段一起成长。

### Architecture Decoupling Discipline / 架构解耦纪律

- The kernel is deeply x86_64-bound (IDT, `int 0x80`, `InterruptFrame`, i8259,
  PIT, ATA PIO). Multi-arch is now a decided goal, so every new stage must consume
  the arch interface (see the Architecture Abstraction track) rather than calling
  x86 mechanisms directly, keeping coupling from leaking further into POSIX code.
- 内核与 x86_64 深度绑定（IDT、`int 0x80`、`InterruptFrame`、i8259、PIT、ATA
  PIO）。多架构已是确定目标，故每个新阶段都必须消费 arch 接口（见「架构抽象」
  轨道），而非直接调用 x86 机制，避免耦合进一步渗入 POSIX 代码。

## Parallel Tracks / 并行轨道

### Architecture Abstraction / 架构抽象层

Goal: grow a thin `arch/` interface so the kernel core stops calling x86_64
mechanisms directly, making future ISAs (e.g. aarch64, riscv64) tractable. This
is a long-running track that should advance incrementally alongside the staged
work, not a single big-bang port.

目标：逐步建立一层薄的 `arch/` 接口，让内核核心不再直接调用 x86_64 机制，使未来的
ISA（如 aarch64、riscv64）成为可行目标。这是一条长期轨道，应与分阶段工作并行、
增量推进，而非一次性大改。

- Define an arch interface for the coupled mechanisms first: CPU/trap frame,
  interrupt controller, timer, context switch, syscall entry, MMU/page-table ops,
  and per-CPU access. Keep x86_64 as the only backend initially.
- 先为耦合机制定义 arch 接口：CPU/trap frame、中断控制器、timer、context switch、
  syscall 入口、MMU/页表操作、per-CPU 访问。初期仅保留 x86_64 一个后端。
- Move x86_64 code under `arch/x86_64/` behind that interface incrementally; do
  not add a second backend until the interface is stable and a real arch consumer
  (a staged feature) exercises it.
- 增量地把 x86_64 代码移到 `arch/x86_64/` 并置于该接口之后；在接口稳定且有真实的
  arch 消费者（某个阶段功能）验证它之前，不引入第二个后端。
- Sequencing: prefer the trap/interrupt/MMU interfaces before Stage 17 (signals)
  and before SMP, since both deepen arch coupling if left implicit. The syscall
  interface should be abstracted before Stage 19 userland wrappers are frozen, so
  the user/kernel ABI is not hard-wired to `int 0x80`.
- 排序：在阶段 17（信号）与 SMP 之前，优先完成 trap/interrupt/MMU 接口，因为两者
  若保持隐式会加深架构耦合。syscall 接口应在阶段 19 用户态 wrapper 冻结前抽象出来，
  避免用户/内核 ABI 被硬绑定到 `int 0x80`。
- Non-goals for now: a second working backend, bi-endian support, or a generic
  device tree. Keep the abstraction driven by real needs, not speculation.
- 当前非目标：第二个可用后端、双端序支持、通用 device tree。让抽象由真实需求驱动，
  而非臆测。

### SMP / 对称多处理

Keep out of the main line until locking, per-CPU state, and TLB shootdown are
explicitly designed first. SMP and the arch abstraction reinforce each other:
per-CPU state and TLB shootdown are inherently arch-specific, so design them
through the arch interface rather than ad hoc x86 code.

在显式设计好锁、per-CPU 状态与 TLB shootdown 之前，不纳入主线。SMP 与架构抽象互为
支撑：per-CPU 状态与 TLB shootdown 本质上是架构相关的，应通过 arch 接口设计，而非
临时的 x86 代码。

### UEFI And Boot Backends / UEFI 与引导后端

- Keep UEFI separate from runtime scheduling/process/VM changes.
- 将 UEFI 与 runtime 调度/进程/VM 变更分离。
- Implement `BOOTX64.EFI`, ESP image generation, QEMU/OVMF smoke, GOP handoff,
  and firmware-table sections only after the unified BootInfo contract is stable;
  preserve the Legacy BIOS path until UEFI reaches parity.
- 在统一 BootInfo 契约稳定后再实现 `BOOTX64.EFI`、ESP 镜像、QEMU/OVMF smoke、
  GOP handoff 与 firmware table section；在 UEFI 达到等价前保留 Legacy BIOS 路径。

### CI And Tooling / CI 与工具链

- Layer validation: source-contract tests, image-generation tests, GCC
  cross-toolchain builds, QEMU headless marker smokes, and optional Bochs
  cross-checks. Keep `uv run pytest` for Python; record explicit skip reasons
  when a tool is unavailable.
- 分层验证：源码契约测试、镜像生成测试、GCC 交叉构建、QEMU headless marker
  smoke、可选 Bochs 交叉检查；Python 用 `uv run pytest`；工具不可用时显式记录
  跳过原因。

### Documentation And OpenSpec / 文档与 OpenSpec

- Keep `docs/en` canonical and mirror in `docs/zh` with matching relative paths.
- `docs/en` 为 canonical，`docs/zh` 按匹配相对路径同步。
- One OpenSpec change per major stage; do not combine scheduler/process/VFS/VM/
  UEFI in a single change. Record architecture, memory-layout, and toolchain
  assumptions, explicit non-goals, and validation notes per change.
- 每个主要阶段创建独立 OpenSpec change，不混合 scheduler/process/VFS/VM/UEFI；
  每个 change 记录架构/内存布局/工具链假设、明确 non-goals 与验证记录。

## Near-Term Recommendation / 近期建议

The full main-line order, with bounded debts now numbered into the sequence.

完整主线顺序，有界债务已编号并入序列。

1. Stage 14.1 (`unify errno`): independent, do this first.
   - 阶段 14.1（统一 errno）：独立，先做。
2. Stage 14.5 (`default-on user-space init`): make normal boot actually run a
   user process; this also kicks off the behavior-assertion testing track.
   - 阶段 14.5（默认进入用户态 init）：让 normal boot 真正运行用户进程；同时启动
     行为断言测试轨道。
3. Then proceed Stage 15 -> 15.5 -> 16 -> 16.5 -> 17 -> 18 -> 19 in order: demand
   paging is the safest first foundation, growable tables (15.5) precede fork
   (16), time & identity (16.5) precede signals (17) and writable FS (18), and
   userland (19) builds on all of it.
   - 然后按阶段 15 -> 15.5 -> 16 -> 16.5 -> 17 -> 18 -> 19 顺序推进：demand paging
     是最安全的第一块地基，可增长表（15.5）先于 fork（16），时间与身份（16.5）
     先于信号（17）与可写 FS（18），用户态（19）建立在全部之上。
4. Advance the Architecture Abstraction track in parallel: land the trap/
   interrupt/MMU interfaces before Stage 17 and SMP, and the syscall interface
   before Stage 19 freezes userland wrappers, keeping x86_64 the only backend
   until a real consumer justifies a second one.
   - 并行推进「架构抽象」轨道：在阶段 17 与 SMP 之前落地 trap/interrupt/MMU 接口，
     在阶段 19 冻结用户态 wrapper 之前落地 syscall 接口；在有真实消费者证明必要前，
     x86_64 保持为唯一后端。
