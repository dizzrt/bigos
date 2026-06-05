# 内核线程与单核调度器

BigOS 阶段 4 引入最小内核线程模型与单核 round-robin 调度器，把系统从单条初始化路径推进到可在多个内核线程之间协作切换的早期运行时。该设施只面向 x86_64 单核、Legacy BIOS、i8259、PIT IRQ0 和 Bochs 验证场景。

## 范围

- 定义内核线程控制块（TCB）：线程 ID、生命周期状态、saved stack pointer、内核栈范围、入口函数与参数、intrusive run queue / terminated list 节点。
- 提供 x86_64 cooperative context switch（`switch_context`），保存/恢复 System V callee-saved 寄存器（rbp、rbx、r12-r15）与栈指针。
- 提供单核 round-robin `yield()`、`sched::start()` 入口与 scheduler-owned idle 线程。
- 用 idle 线程替代 `kernel()` 尾部裸 `hlt` loop。
- timer IRQ0 只通过 bounded、IRQ-context-safe 的 `sched::on_timer_tick()` 记录 reschedule intent。

## 非目标与边界

- **单核**：没有 SMP balancing、per-CPU run queue、IPI、affinity 或跨 CPU 同步。
- **无用户态**：没有 ring3 切换、进程模型、syscall ABI、地址空间切换或用户程序加载。
- **无阻塞 sleep**：没有优先级调度、CFS、实时调度、wait queue、sleep queue、阻塞 IO 或 scheduler sleep。线程状态只在 runnable、running、idle、terminated 之间。
- 不改变 boot 固定地址、higher-half base、kernel load base、BootInfo ABI、direct map、`KVMEM_BASE`、IDT vector 分配或 `InterruptFrame` ABI。

## 线程模型与状态

每个内核线程由一个 TCB 表示，记录稳定 thread ID、`ThreadState`、saved stack pointer（cooperative context 指针）、内核栈 base/size、入口函数与参数。`ThreadState` 限定为 `Runnable`、`Running`、`Idle`、`Terminated`，任何状态都不暗示阻塞 IO、用户 wait queue、进程归属或 SMP 迁移。

run queue 与 terminated list 都是 intrusive 链表，节点（`rq_next`、`term_next`）由 TCB 自身持有，生命周期与 TCB 绑定。因此调度路径不依赖普通 heap 容器，也不需要在 IRQ handler 中分配队列节点。

## Allocator 上下文契约

`create_kernel_thread()` 只能在非中断上下文调用。它通过阶段 3 的普通 allocator 契约（`kmalloc()` 取 TCB、`alloc_kernel_pages()` 取栈）分配资源；timer IRQ0、keyboard IRQ1、`#PF` 与 `irq_dispatch` 路径都不创建、不释放线程对象，也不做普通动态分配。创建失败路径只释放本路径已分配的资源（在非中断上下文），不破坏 allocator 契约。

阶段 4 普通内核线程默认固定使用 1 页内核栈，TCB 记录 stack base/size。该默认页数不暴露 smoke/debug 构建开关；后续若需要更大栈或 guard page，应在单独 change 中结合栈溢出诊断推进。

## Context Switch

`switch_context(uint64_t *old_sp, uint64_t new_sp)`（`src/kernel/sched/switch.s`）保存当前线程的 callee-saved 寄存器与栈指针到 `*old_sp`，加载目标线程栈并 `ret` 进入其保存的返回点。它不改变 `InterruptFrame` layout、生成的 ISR entry frame 或 EOI/`iretq` 路径，只用于非中断上下文的 cooperative yield/exit。

新线程栈由 `create_kernel_thread()` 预构造：首次被调度时 `switch_context` 的 `ret` 进入 scheduler-owned `thread_trampoline`，trampoline 先开启中断，再调用线程入口函数；入口返回时进入 `thread_exit()`。

调用方在 `switch_context` 前 `cli`，每个 resume 点在切换返回后 `sti`，保证切换关键区不被 IRQ 打断。

## 调度策略与 Idle

`yield()` 是单核 round-robin 协作切换：若存在至少一个其他可运行线程，则把当前线程放回 run queue 尾部并切换到下一个 runnable 线程；若没有其他可运行线程，则继续运行当前线程或 idle，不破坏 run queue。

`sched::start()` 把 boot/main 执行上下文收编为 scheduler-owned idle 线程，复用现有 boot 栈，然后进入 idle 循环：先 `yield()` 运行可运行线程，否则执行 `hlt` 等待 IRQ 唤醒后重新评估。idle 的 `hlt` 必须在 IRQ enabled 状态下运行，因此 `start()` 在 `irq::enableIRQ()` 之后调用，timer IRQ0 才能唤醒 CPU。

## 线程退出与延后回收

`thread_exit()` 把当前线程标记为 `Terminated`、移出 runnable 调度，并挂入 scheduler-owned terminated list，但**不**在退出栈上立即释放当前线程的 TCB 或内核栈。安全回收推迟到后续 lifecycle change；当前阶段只保证 terminated 线程不会再次进入 runnable queue，且阶段 4 线程数量 bounded。

## Timer 与 IRQ 边界

timer IRQ0 handler 继续通过 `bigos::timer::on_tick()` 推进 tick，然后调用 bounded、IRQ-context-safe 的 `bigos::sched::on_timer_tick()`，后者只递增一个 reschedule intent 计数，不分配、不释放、不阻塞、不做 IO、不切换线程。阶段 4 不实现 IRQ 返回前抢占切换：线程切换只发生在非中断上下文的 `yield()`/`thread_exit()`。IRQ dispatch 仍按既有 EOI、saved frame、register restore 和 `iretq` 语义返回。CPU exception 路径保持 diagnostic-only，不接入线程恢复、唤醒或重试。

## 验证 Smoke

`scheduler_smoke` 默认关闭。启用 `BIGOS_SCHEDULER_SMOKE` 时，`kernel()` 创建两个 worker 线程，各输出固定次数的 `BIGOS_SCHED_THREAD_A` / `BIGOS_SCHED_THREAD_B` marker 并通过 `yield()` 交替运行，证明两个内核线程可协作切换。普通 boot 不创建 smoke 线程，也不输出 scheduler marker。
