# 内核线程与 per-CPU 调度器

BigOS 仍保持最小内核线程模型的 bounded 边界，但调度器现在为每个 online CPU 拥有一个 run queue domain。普通内核线程仍使用相同 TCB 与 cooperative switch frame；每个 CPU 自己拥有 current thread、idle thread、runnable queue、sleep list、terminated list、reschedule state 与 time-slice accounting。PIT 或 LAPIC timer interrupt 可以让本 CPU time slice 到期，并请求受 guard 保护的 IRQ-return switch。

## 范围

- 定义内核线程控制块（TCB）：线程 ID、生命周期状态、saved stack pointer、内核栈范围、入口函数与参数、intrusive run queue、wait/sleep 与 terminated list 节点。
- 通过 `bigos::arch_context::switch_kernel_context()` 提供 scheduler-facing
  kernel context-switch boundary。当前实现进入 x86_64 `switch_context` assembly
  primitive，保存/恢复 System V callee-saved 寄存器（rbp、rbx、r12-r15）与栈指针。
- 提供 CPU-local round-robin `yield()`、`sched::start()` 入口与 scheduler-owned idle 线程。
- 用 idle 线程替代 `kernel()` 尾部裸 `hlt` loop。
- CPU-local timer interrupt 通过 bounded、IRQ-context-safe 的 `sched::on_timer_tick()` 统计普通线程 time slice、记录 reschedule intent 并唤醒到期 sleeper。
- 只允许外部 IRQ 在注册 handler 完成、单次 i8259 EOI 已发送、且 scheduler preemption guard 允许后，于 IRQ return 边界切换线程。
- 通过 `create_kernel_thread()` 提供 bounded 初始 placement，并通过 `create_kernel_thread_on_cpu()` 提供显式目标 CPU placement。remote enqueue 先在目标 CPU scheduler lock 下发布 runnable state，再设置目标 CPU reschedule flag 并发送 scheduler nudge。

## 非目标与边界

- **仅限 scheduler SMP**：支持 per-CPU run queue、scheduler nudge、LAPIC timer ownership，以及已支持 timer/keyboard IRQ source 的 bounded APIC default delivery；不支持 CPU hotplug、NUMA、完整 IRQ affinity API、work stealing、CFS、实时调度、POSIX scheduling policy、generic device IRQ balancing 或 MSI/MSI-X。
- **无完整优先级调度器**：调度器只记录 bounded priority/policy metadata，默认选择策略仍是确定性的单核 round-robin。
- **Bounded process 与 syscall integration**：timer preemption 不创建用户可见的 POSIX scheduling policy。后续 process/fd/VFS syscall 可以在普通进程 syscall 上下文中使用 `sched::can_block()`，但只限同一个单核 blocking contract。
- **bounded timer-driven scheduler semantics 阻塞边界**：wait queue 与 tick-based sleep 仍是内核线程原语。它们可以通过 waiter-owned CPU scheduler domain 唤醒，但仍不提供 POSIX blocking IO policy、用户可见 cancellation 或 signal 语义。
- 不改变 boot 固定地址、higher-half base、kernel load base、BootInfo ABI、direct map、`KVMEM_BASE`、IDT vector 分配或 `InterruptFrame` ABI。

## 线程模型与状态

每个内核线程由一个 TCB 表示，记录稳定 thread ID、`ThreadState`、saved stack pointer（cooperative context 指针）、内核栈 base/size、入口函数与参数、bounded time-slice state，以及预留的 priority/policy metadata。`ThreadState` 包含 `Runnable`、`Running`、`Idle`、`Blocked`、`Sleeping` 和 `Terminated`。`Blocked` 与 `Sleeping` 只是 bounded non-runnable 内核等待状态，不暗示 POSIX blocking IO、用户 wait queue、进程归属、取消策略或 SMP 迁移。

run queue、wait queue、sleep list 与 terminated list 都是 intrusive 链表，节点（`rq_next`、`wait_next`、`sleep_next`、`term_next`）由 TCB 自身持有，生命周期与 TCB 绑定。因此调度路径不依赖普通 heap 容器，也不会在 IRQ handler 中分配队列节点。同一线程最多同时属于一个 CPU run queue、一个显式 wait queue 和一个 timeout tracking list。

## 阻塞上下文契约

`sched::can_block()` 只允许 `sched::start()` 之后的普通 running kernel thread 在 maskable IRQ enabled、且不处于 IRQ/exception/syscall dispatch、fatal diagnostic 或 scheduler critical section 时阻塞。违反契约时 blocking API 返回确定性负 wait error，不会在禁止上下文中静默 busy-wait 或把当前线程入队。

`sched::enter_nonblocking_context()` / `sched::leave_nonblocking_context()` 将禁止阻塞的 dispatch 区域标记为 nonblocking。该 guard 覆盖 timer IRQ0、keyboard IRQ1、CPU exception，以及非进程或不受支持的 `int 0x80` 上下文，避免它们调用 wait API。`sched::disable_preemption()` / `sched::enable_preemption()` 提供独立的 bounded timer-preemption guard；scheduler critical section 使用该 guard，因此 run queue、wait queue、sleep list、状态转换与 switch preparation 不会被 IRQ-return preemption 打断。普通进程 syscall 在保留 IF 且 scheduler 已启动时可以通过 `sched::can_block()`；不支持的上下文仍走确定性 nonblocking error path。

## Wait Queue 与 Sleep

`sched::WaitQueue` 是由调用方持有的 head/tail 小对象，其成员指向 scheduler-owned TCB。`sched::wait_queue_wait_until()` 会在 IRQ disabled 状态下检查可选 predicate，先记录 queue membership，再把当前线程切换为 `Blocked` 或 `Sleeping`，随后协作式切换到其他 runnable 线程或 idle 线程。predicate 检查避免 empty-buffer check 与入队之间漏掉 producer wakeup。

`sched::wake_one()` 和 `sched::wake_all()` 不分配内存，可从 bounded IRQ-safe producer 路径调用。wakeup 会把选中 waiter 从 wait queue 和 timeout tracking 中各移除一次，将其改回 `Runnable`，并追加到 waiter-owned CPU run queue，等待后续 cooperative 或 guarded IRQ-return scheduling point。目标 CPU 是远端时，scheduler 会先设置该 CPU 的 reschedule-pending state，再发送 scheduler nudge。

`sched::sleep_for()` 与 `timer::sleep_for()` 使用现有 monotonic tick，让当前线程阻塞到 deadline 到期。到期 sleeper 由当前 CPU 的 `sched::on_timer_tick()` 通过 bounded intrusive list 扫描唤醒；IRQ 路径不分配、不释放、不阻塞、不 bulk 输出、不访问 filesystem，也不切换线程。

## Allocator 上下文契约

`create_kernel_thread()` 只能在非中断上下文调用。它通过 kernel memory API 的普通 allocator 契约（`kmalloc()` 取 TCB、`alloc_kernel_pages()` 取栈）分配资源；timer IRQ0、keyboard IRQ1、`#PF` 与 `irq_dispatch` 路径都不创建、不释放线程对象，也不做普通动态分配。创建失败路径只释放本路径已分配的资源（在非中断上下文），不破坏 allocator 契约。

普通内核线程默认固定使用 1 页内核栈，TCB 记录 stack base/size。该默认页数不暴露 smoke/debug 构建开关；后续若需要更大栈或 guard page，应在单独 change 中结合栈溢出诊断推进。

## Context Switch

`bigos::arch_context::switch_kernel_context()` 是 scheduler-facing context
primitive。它把 saved stack pointer 视为 scheduler-owned opaque kernel context
token，并在当前 x86_64 backend 进入 `switch_context(uint64_t *old_sp,
uint64_t new_sp)`（`kernel/core/sched/switch.s`）。assembly 会把当前线程的
callee-saved 寄存器与栈指针保存到 `*old_sp`，加载目标线程栈并 `ret` 进入其保存的
返回点。cooperative `yield()` / `thread_exit()` 与 IRQ-return preemption 都使用同一
边界，而不是 open-code assembly frame offset。IRQ-return preemption 使用
scheduler-owned bridge，在 EOI 之后保存被中断线程的当前内核栈 continuation，稍后
恢复它，让原始 `InterruptFrame` 继续由 `isr_common` 恢复并通过 `iretq` 返回。

新线程栈由 `create_kernel_thread()` 预构造：首次被调度时 `switch_context` 的 `ret` 进入 scheduler-owned `thread_trampoline`，trampoline 先开启中断，再调用线程入口函数；入口返回时进入 `thread_exit()`。

调用方在 `switch_context` 前 `cli`，每个 resume 点在切换返回后 `sti`，保证切换关键区不被 IRQ 打断。

## 调度策略与 Idle

`yield()` 是 CPU-local round-robin 协作切换：若当前 CPU run queue 中存在至少一个其他可运行线程，则把当前线程放回该 run queue 尾部并切换到下一个 runnable 线程；被选中的普通线程获得固定 bounded time slice。blocked、sleeping、idle 和 terminated 线程不会参与普通 runnable 选择。若本地没有其他可运行线程，则继续运行当前线程或 idle，不破坏 run queue。

`sched::start()` 把调用 CPU 的执行上下文收编为该 CPU 的 scheduler-owned idle 线程。BSP 复用现有 boot 栈；AP 在 AP-local scheduler domain 初始化后复用 startup stack。idle 循环先 `yield()` 运行本地可运行线程，否则执行 `hlt` 等待 timer 或 scheduler nudge 唤醒后重新评估。idle 的 `hlt` 必须在 IRQ enabled 状态下运行。

## 线程退出与延后回收

`thread_exit()` 先移除任何残留 wait/sleep membership，再把当前线程标记为 `Terminated`、移出 runnable 调度，并挂入 scheduler-owned terminated list，但**不**在退出栈上立即释放当前线程的 TCB 或内核栈。安全回收推迟到后续 lifecycle change；当前调度边界只保证 terminated 线程不会再次进入 runnable queue，且内核线程数量 bounded。

## Timer 与 IRQ 边界

BSP timer 路径继续通过 `bigos::timer::on_tick()` 推进全局 tick，然后调用 bounded、IRQ-context-safe 的 `bigos::sched::on_timer_tick()`。AP local timer handler 在记录 CPU-local tick 后调用同一个 scheduler hook 做 AP-local time-slice accounting。scheduler hook 会递减当前普通线程 time slice，在到期时记录 CPU-local pending reschedule intent，并通过 allocation-free intrusive state 唤醒本 CPU 到期 sleeper；它不分配、不释放、不阻塞、不 bulk 输出，也不直接从 timer handler 切换线程。

外部 IRQ dispatch 继续集中拥有 EOI。注册 handler 返回后，`irq_dispatch` 发送恰好一次
owner-specific EOI，然后调用 `sched::maybe_preempt_on_irq_return()`。PIC fallback IRQ
发送 i8259 EOI；LAPIC timer、scheduler-nudge IPI、TLB-shootdown IPI 和已支持的
IOAPIC external IRQ 发送 LAPIC EOI。该 bridge 只在 preemption enabled、
`bigos::arch_context` 判断为 kernel IRQ-return context、当前线程仍是普通 running
线程、且存在 runnable peer 时切换。scheduler nudge 只表示重新观察调度状态，不是 CPU
hotplug、generic device IRQ balancing 或 MSI/MSI-X。CPU exception 与 `int 0x80`
syscall 从不进入该 bridge，不发送 irqchip EOI，也不接入 sleep/process-lifecycle
recovery 路径。

## 验证 Smoke

`scheduler_smoke` 默认关闭。启用 `BIGOS_SCHEDULER_SMOKE` 时，`kernel()` 创建两个 worker 线程，各输出固定次数的 `BIGOS_SCHED_THREAD_A` / `BIGOS_SCHED_THREAD_B` marker 并通过 `yield()` 交替运行，证明两个内核线程可协作切换。普通 boot 不创建 smoke 线程，也不输出 scheduler marker。

`blocking_smoke` 也默认关闭。启用 `BIGOS_BLOCKING_SMOKE` 时，`kernel()` 创建一个 blocking reader 和一个 synthetic TTY producer。smoke 输出 `BIGOS_BLOCKING_WAIT_BLOCKED`、`BIGOS_BLOCKING_WAKE_SENT`、`BIGOS_BLOCKING_WAIT_RESUMED`、`BIGOS_BLOCKING_TIMEOUT_BLOCKED`、`BIGOS_BLOCKING_TIMEOUT_EXPIRED` 与 `BIGOS_BLOCKING_SMOKE_PASSED`，在不依赖手工键盘输入的情况下验证 wait queue wakeup、timeout sleep 和 cooperative resume。

`scheduler_semantics_smoke` 默认关闭。启用 `BIGOS_SCHEDULER_SEMANTICS_SMOKE` 时，`kernel()` 创建两个普通内核线程。第一个线程在 preemption disabled 且 timer ticks 继续推进时观察 `BIGOS_SCHED_SEMANTICS_PREEMPT_DELAYED`；第二个线程只有在 timer-driven IRQ-return preemption 真正运行它时才能输出 `BIGOS_SCHED_SEMANTICS_PREEMPTED` 与 `BIGOS_SCHED_SEMANTICS_PASSED`。这些 marker 与 cooperative `scheduler_smoke` marker 区分开。

`scheduler_smp_smoke` 默认关闭，并隐式启用 AP startup/per-CPU timer baseline 与 bounded APIC default delivery gate。启用 `BIGOS_SCHEDULER_SMP_SMOKE` 时，`kernel()` 创建一个 BSP worker 和一个显式投递到 AP 的 worker。只有 APIC-backed timer/keyboard routing active 且普通 scheduler work 在超过一个 online CPU 上运行时，smoke 才输出 `BIGOS_APIC_DEFAULT_DELIVERY_ACTIVE`、`BIGOS_SCHED_SMP_BSP_THREAD`、`BIGOS_SCHED_SMP_AP_THREAD` 与 `BIGOS_SCHED_SMP_PASSED`。该 smoke 不验证 CPU hotplug、NUMA、generic device IRQ balancing、MSI/MSI-X 或完整 IRQ affinity。
