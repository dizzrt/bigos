# BigOS Roadmap / BigOS 路线图

Language: English | 简体中文

This roadmap summarizes the completed baseline from the local planning note and
sets the recommended direction for the next project stages. It is a planning
document, not a replacement for `docs/`, `openspec/specs/`, or archived
OpenSpec changes.

本文档压缩整理本地规划笔记中的已完成基线，并给出后续阶段建议。它是规划文档，
不替代 `docs/`、`openspec/specs/` 或已归档的 OpenSpec change。

## Completed Baseline / 已完成基线

BigOS has completed the early bring-up path through a smoke-tested single-core
kernel with a minimal user-mode loop:

BigOS 已完成早期 bring-up，并形成一个带 smoke 验证的单核内核和最小用户态闭环：

- Boot: Legacy BIOS/MBR/exFAT boot sectors load `/boot/boot.bin`, then load the
  higher-half ELF64 kernel named `kernel`.
- 引导：Legacy BIOS/MBR/exFAT boot sector 加载 `/boot/boot.bin`，再加载名为
  `kernel` 的高半区 ELF64 内核。
- Runtime core: VGA/COM1 output, kernel-owned IDT, exception/IRQ/syscall
  dispatch, i8259 PIC, PIT IRQ0 tick, keyboard IRQ1 to TTY/console, a
  single-core scheduler with time-slice/preemption-disable semantics, explicit
  blocking primitives, and `int 0x80` syscall dispatch are implemented.
- 运行时核心：已实现 VGA/COM1 输出、内核自有 IDT、exception/IRQ/syscall 分发、
  i8259 PIC、PIT IRQ0 tick、键盘 IRQ1 到 TTY/console、带 time-slice 与
  preemption-disable 语义的单核调度器、显式阻塞原语和 `int 0x80` syscall 分发。
- Blocking model: thread wait states, wait queues, wake-one/wake-all, timeout
  waits, blocking-context guards, timer-backed sleep, and blocking TTY consumer
  paths are implemented under the single-core scheduler boundary.
- 阻塞模型：已在单核调度边界内实现线程等待状态、wait queue、wake-one/wake-all、
  timeout wait、阻塞上下文保护、timer-backed sleep 和阻塞式 TTY consumer 路径。
- Memory: buddy, slab/kmalloc, kernel virtual memory, direct map, user
  address-space derivation, safe user teardown, and owned empty PT/PD/PDPT
  reclamation are implemented.
- 内存：已实现 buddy、slab/kmalloc、内核虚拟内存、direct map、用户地址空间派生、
  安全用户态 teardown，以及 owned 空 PT/PD/PDPT 回收。
- Storage/FS: synchronous read-only ATA PIO reads, MBR exFAT partition discovery,
  read-only exFAT mount/path lookup, bounded file reads, a minimal read-only
  fd/VFS shell, `open`/`read`/`close` syscalls, and `fs_smoke` are implemented.
- 存储/文件系统：已实现同步只读 ATA PIO 读取、MBR exFAT 分区发现、只读 exFAT
  mount/path lookup、bounded file read、最小只读 fd/VFS 壳层、`open`/`read`/`close`
  syscall 和 `fs_smoke`。
- User mode: the normal process lifecycle core is buildable outside smoke-only
  configurations, with bounded PID allocation, process table ownership,
  parent/child linkage, `wait`/`exit`, safe zombie/reaper teardown, and a general
  bounded ELF64 `exec` path with basic `argv`/`envp`. The flat first-user-program
  smoke and filesystem-backed ELF64 `ET_EXEC` smoke remain default-off consumers
  behind `user_program_smoke` and `user_elf_smoke`.
- 用户态：常规进程生命周期核心已可在非 smoke 配置下构建，包含 bounded PID 分配、
  进程表所有权、父子关系、`wait`/`exit`、安全 zombie/reaper teardown，以及带基础
  `argv`/`envp` 的通用 bounded ELF64 `exec` 路径。flat 首个用户程序 smoke 和基于
  文件系统的 ELF64 `ET_EXEC` smoke 仍是默认关闭的 `user_program_smoke` 与
  `user_elf_smoke` 消费者。
- Build/run: `xmake` is the primary build path; smoke options are configured with
  `xmake f ...=y`; QEMU/Bochs helper paths are available through `xmake run` and
  `tools/boot_debug.py`.
- 构建/运行：`xmake` 是主构建路径；通过 `xmake f ...=y` 配置 smoke 选项；
  QEMU/Bochs helper 路径由 `xmake run` 和 `tools/boot_debug.py` 提供。
- Validation: the stage 9 runtime smoke validation matrix is productized with
  QEMU headless serial-marker checks, per-case timeouts, structured validation
  artifacts, explicit skip/block reasons, blocking primitive and scheduler
  semantics smoke coverage, and scenario-specific Bochs or QEMU+Bochs
  cross-validation guidance.
- 验证：阶段 9 runtime smoke 验证矩阵已产品化，包含 QEMU headless 串口 marker
  检查、按 case 配置的 timeout、结构化验证 artifact、明确的 skipped/blocked
  原因、blocking primitive 与 scheduler semantics smoke 覆盖，以及面向特定场景的
  Bochs 或 QEMU+Bochs 交叉验证指引。

## Current Boundary / 当前边界

The project is now a controlled research kernel rather than a general-purpose OS.
Most advanced capabilities are still smoke-scoped, single-core, synchronous, or
kernel-only.

项目当前是一个受控研究内核，还不是通用操作系统。多数高级能力仍处于 smoke 级、
单核、同步或仅内核可用的边界内。

```text
Implemented loop / 已形成闭环

BIOS boot
  -> higher-half kernel
  -> memory/runtime init
  -> IRQ/timer/TTY
  -> scheduler semantics
  -> blocking/sleep primitives
  -> int 0x80 syscall
  -> optional ring3 smoke
  -> optional exFAT-backed ELF smoke
  -> minimal fd/VFS shell

Missing general OS layer / 尚缺通用 OS 层

page cache
  -> VMA/demand paging/COW
  -> libc/userland
  -> CI/release-grade validation
```

## Recommended Stages / 推荐阶段

### Stage 9: Productize Runtime Validation / 阶段 9：运行时验证产品化

Status: completed and archived as `productize-runtime-smoke-validation`.

状态：已完成，并以 `productize-runtime-smoke-validation` 归档。

Goal: keep the existing smoke switches and helper scripts organized as a
repeatable validation matrix.

目标：将现有 smoke 开关和 helper 脚本保持为可重复执行的验证矩阵。

- Add a smoke matrix for the narrowest useful combinations of memory, timer,
  scheduler, syscall, filesystem, user program, and user ELF checks.
- 增加 smoke 矩阵，覆盖 memory、timer、scheduler、syscall、filesystem、
  user program、user ELF 的最小有价值组合。
- Prefer QEMU headless marker checks for automated validation; keep Bochs or
  QEMU+Bochs cross-validation for boot, IRQ, timer, ATA PIO, and port-IO changes.
- 自动化验证优先使用 QEMU headless marker；boot、IRQ、timer、ATA PIO 和
  port IO 变更保留 Bochs 或 QEMU+Bochs 交叉验证。
- Record tool availability, skipped validation, serial markers, logs, and
  residual risk in a structured validation artifact.
- 结构化记录工具可用性、跳过的验证、串口 marker、日志路径和剩余风险。

### Stage 10: Blocking And Sleep Primitives / 阶段 10：阻塞与睡眠原语

Status: completed and archived as `introduce-kernel-blocking-primitives`.

状态：已完成，并以 `introduce-kernel-blocking-primitives` 归档。

Goal: define the kernel waiting model before expanding process, filesystem, or
device-driver behavior.

目标：在扩展进程、文件系统或设备驱动行为前，先定义内核等待模型。

- Introduce thread states, sleep queues, wakeup, timeout waits, and explicit
  rules for contexts that must not block.
- 引入线程状态、sleep queue、wakeup、超时等待，以及禁止阻塞的上下文规则。
- Keep the first version single-core and cooperative; avoid mixing this stage
  with full preemption or SMP.
- 第一版保持单核与协作式语义，避免同时引入完整抢占或 SMP。
- Use TTY input, timer waits, and future process wait/exit as the first consumers.
- 以 TTY 输入、timer wait 和未来进程 wait/exit 作为第一批消费者。

### Stage 11: Scheduler Semantics Upgrade / 阶段 11：调度语义升级

Status: completed and archived as `upgrade-scheduler-semantics`.

状态：已完成，并以 `upgrade-scheduler-semantics` 归档。

Goal: evolve the cooperative scheduler toward timer-driven preemption while
preserving the existing interrupt and context-switch ABI.

目标：在保持现有中断与 context-switch ABI 的前提下，将协作式调度推进到
timer 驱动抢占。

- Add time slices, priorities or priority hooks, reschedule-on-IRQ-return, and
  clear critical-section/preemption-disable rules.
- 增加时间片、优先级或优先级预留点、IRQ return 触发重调度，以及明确的临界区和
  preemption-disable 规则。
- Audit `InterruptFrame`, context-switch frame layout, idle-thread ownership,
  and IRQ EOI ordering before implementation.
- 实现前审查 `InterruptFrame`、context-switch frame 布局、idle 线程所有权和
  IRQ EOI 顺序。
- Keep SMP out of this stage unless locking, per-CPU state, and TLB shootdown are
  explicitly designed first.
- 除非先设计锁、per-CPU 状态和 TLB shootdown，否则本阶段不引入 SMP。

### Stage 12: Process Lifecycle / 阶段 12：进程生命周期

Status: completed and archived as `introduce-process-lifecycle`.

状态：已完成，并以 `introduce-process-lifecycle` 归档。

Goal: promote the smoke-only user process path into a normal kernel subsystem.

目标：把 smoke-only 用户进程路径提升为常规内核子系统。

- Make the process core buildable outside smoke-only configurations while
  preserving safe CR3/root ownership and teardown boundaries.
- 让进程核心可在非 smoke 配置下构建，同时保持安全的 CR3/root 所有权和 teardown
  边界。
- Add PID allocation, process table policy, parent/child relationships,
  `wait`/`exit`, and a general `exec` path with `argv`/`envp` basics.
- 增加 PID 分配、进程表策略、父子关系、`wait`/`exit`，以及带基础
  `argv`/`envp` 的通用 `exec` 路径。
- Defer `fork`, COW, signals, and broad POSIX policy until VM and lifecycle rules
  are stable.
- 在 VM 与生命周期规则稳定前，暂缓 `fork`、COW、signal 和广泛 POSIX 策略。

### Stage 13: File Descriptors And VFS Shell / 阶段 13：文件描述符与 VFS 壳层

Status: completed and archived as `introduce-fd-vfs-shell`.

状态：已完成，并以 `introduce-fd-vfs-shell` 归档。

Goal: provide a stable kernel/user I/O boundary before writable filesystems and
page cache.

目标：在可写文件系统和 page cache 前，先提供稳定的内核/用户 I/O 边界。

- Define vnode/file/file-table concepts and mount the existing read-only exFAT
  implementation behind a minimal VFS interface.
- 定义 vnode/file/file table 概念，并将现有只读 exFAT 实现接入最小 VFS 接口。
- Add `open`, `read`, `close`, and simple file descriptor inheritance rules for
  `exec`.
- 增加 `open`、`read`、`close`，以及 `exec` 的简单文件描述符继承规则。
- Defer write support, directory mutation, permissions, page cache, and async I/O
  until blocking and lifecycle semantics are reliable.
- 在阻塞与生命周期语义可靠前，暂缓写入、目录变更、权限、page cache 和异步 I/O。

### Stage 14: VMA And User Memory API / 阶段 14：VMA 与用户内存 API

Goal: introduce user virtual-memory policy before demand paging and COW.

目标：在 demand paging 和 COW 前，引入用户虚拟内存策略。

- Add VMA tracking, `brk`, anonymous mappings, stack-growth policy, and user
  range validation based on VMAs rather than only page-table probes.
- 增加 VMA 跟踪、`brk`、匿名映射、用户栈增长策略，并基于 VMA 而不只是页表探测做
  用户地址范围验证。
- Introduce demand paging after page-fault recovery, allocation failure behavior,
  and process-kill semantics are designed.
- 在设计好 page fault recovery、分配失败行为和进程 kill 语义后，再引入
  demand paging。
- Plan COW together with `fork`; do not bolt it onto the current bounded ELF
  smoke path prematurely.
- 将 COW 与 `fork` 一起规划，不要过早嫁接到当前 bounded ELF smoke 路径上。

## Parallel Tracks / 并行轨道

### UEFI And Boot Backends / UEFI 与引导后端

- Keep UEFI separate from runtime scheduling, process, and VM changes.
- 将 UEFI 与 runtime 调度、进程和 VM 变更分离。
- Implement `BOOTX64.EFI`, ESP image generation, QEMU/OVMF smoke flow, GOP
  framebuffer handoff, and firmware-table sections only after the unified
  BootInfo contract is stable.
- 在统一 BootInfo 契约稳定后，再实现 `BOOTX64.EFI`、ESP 镜像生成、
  QEMU/OVMF smoke、GOP framebuffer handoff 和 firmware table section。
- Preserve the current Legacy BIOS path until the UEFI backend reaches feature
  parity for kernel entry and diagnostics.
- 在 UEFI 后端达到内核入口和诊断能力等价前，保留当前 Legacy BIOS 路径。

### CI And Tooling / CI 与工具链

- Layer validation into source-contract tests, image-generation tests, GCC
  cross-toolchain builds, QEMU headless marker smokes, and optional Bochs
  cross-checks.
- 将验证分层为源码契约测试、镜像生成测试、GCC 交叉工具链构建、QEMU headless
  marker smoke，以及可选 Bochs 交叉检查。
- Keep `uv run pytest` for Python tests and helper validation.
- Python 测试和 helper 验证继续使用 `uv run pytest`。
- Add explicit skip reasons when cross-binutils, QEMU, Bochs, ROM/display
  configuration, or compile database support is unavailable.
- 当 cross-binutils、QEMU、Bochs、ROM/display 配置或 compile database 支持不可用时，
  显式记录跳过原因。

### Documentation And OpenSpec / 文档与 OpenSpec

- Keep `docs/en` canonical and update `docs/zh` with matching relative paths for
  repository documentation.
- 仓库文档以 `docs/en` 为 canonical，并同步更新 `docs/zh` 中匹配的相对路径。
- Create one OpenSpec change per major runtime stage; avoid combining scheduler,
  process, VFS, VM, and UEFI work in a single change.
- 每个主要 runtime 阶段创建独立 OpenSpec change，避免把 scheduler、process、VFS、
  VM 和 UEFI 混入同一个 change。
- Include architecture assumptions, memory-layout assumptions, emulator/toolchain
  assumptions, explicit non-goals, and validation notes in each change.
- 每个 change 记录架构假设、内存布局假设、emulator/toolchain 假设、明确非目标和验证记录。

## Near-Term Recommendation / 近期建议

With process lifecycle and fd/VFS archived, the next change should broaden VM
policy before userland runtime expectations:

随着进程生命周期和 fd/VFS 均已归档，下一项 change 应先扩展 VM 策略，再提高用户态
runtime 期望：

1. `introduce-vma-user-memory-api`
   - Choose this after process lifecycle and early file-descriptor ownership are
     stable enough to define `brk`, anonymous mappings, stack-growth policy, and
     VMA-backed user range validation.
   - 当进程生命周期和早期文件描述符所有权足够稳定后，再选择它来定义 `brk`、匿名映射、
     用户栈增长策略和基于 VMA 的用户地址范围验证。
2. `plan-userland-runtime`
   - Choose this after fd/VFS and VMA basics are explicit enough to define the
     smallest libc-adjacent startup, syscall wrappers, and user test binaries.
   - 在 fd/VFS 与 VMA 基础足够明确后，再选择它来定义最小 libc-adjacent 启动流程、
     syscall wrapper 和用户态测试二进制。

VMA and demand-paging work remain safer after lifecycle and I/O ownership rules
are stable, and userland runtime work should wait until both boundaries are
explicit.

生命周期与 I/O 所有权规则稳定后，再推进 VMA/demand paging 会更安全；userland
runtime 应等两个边界都明确后再启动。
