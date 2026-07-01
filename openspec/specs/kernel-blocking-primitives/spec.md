## Purpose

Define the BigOS blocking primitive contract for explicit wait states, wait queues, forbidden blocking contexts, timeout waits, and reproducible validation under the single-core cooperative scheduler model.

## Requirements

### Requirement: 线程等待状态是显式且有界的
BigOS SHALL provide explicit single-core thread wait states for blocking primitives, without introducing preemptive scheduling, SMP migration, process ownership, or POSIX blocking policy.

#### Scenario: 线程进入等待状态
- **WHEN** 非中断内核代码把当前线程挂入一个 wait queue 或 sleep queue
- **THEN** BigOS MUST record the thread as blocked, sleeping, or an equivalent bounded non-runnable wait state
- **AND** the thread MUST NOT remain eligible for normal runnable scheduling until a wakeup, timeout, cancellation, or termination transition occurs

#### Scenario: 等待状态保持单核边界
- **WHEN** scheduler or blocking APIs describe wait states
- **THEN** the documented behavior MUST state that they are single-core cooperative states
- **AND** they MUST NOT imply SMP migration, IPI, per-CPU run queues, preemptive IRQ-return switching, process wait ownership, or user-visible POSIX semantics

### Requirement: wait queue 支持阻塞与唤醒
BigOS SHALL provide a minimal wait queue primitive that allows non-interrupt kernel code to block the current thread and allows eligible producers to wake one or more waiting threads.

#### Scenario: 非中断线程阻塞在 wait queue
- **WHEN** current thread calls a blocking wait API from a context where blocking is allowed
- **THEN** BigOS MUST atomically record the thread's wait queue membership before making the thread non-runnable
- **AND** the scheduler MUST switch to another runnable thread or the idle thread without corrupting the run queue

#### Scenario: wakeup 让等待线程重新 runnable
- **WHEN** a wakeup targets a wait queue that contains one or more blocked threads
- **THEN** BigOS MUST remove the selected thread or threads from the wait queue
- **AND** BigOS MUST transition them back to runnable state exactly once
- **AND** wakeup MUST NOT require ordinary dynamic allocation

#### Scenario: 空队列唤醒是安全的
- **WHEN** a wakeup targets an empty wait queue
- **THEN** BigOS MUST return deterministically without corrupting queue state, scheduler state, or interrupt state

### Requirement: 禁止阻塞上下文是显式的
BigOS SHALL define and enforce contexts where blocking, sleeping, or waiting is forbidden.

#### Scenario: IRQ handler 不能阻塞
- **WHEN** timer IRQ0, keyboard IRQ1, external IRQ dispatch, CPU exception dispatch, or syscall/interrupt entry code runs in a context marked non-blocking
- **THEN** BigOS MUST NOT call blocking wait APIs, sleep APIs, `mdelay()` as a substitute for sleeping, or APIs that can schedule away the current thread
- **AND** source-level checks MUST cover representative forbidden call sites

#### Scenario: 不可阻塞上下文调用 wait API
- **WHEN** kernel code calls a blocking wait API while interrupts are disabled, inside an IRQ/exception handler, inside a scheduler critical section, or inside a fatal diagnostic path
- **THEN** BigOS MUST fail deterministically through a documented error, assertion, or panic path
- **AND** it MUST NOT silently enqueue the current thread or enter an unbounded busy wait

### Requirement: timeout wait 基于 monotonic tick
BigOS SHALL provide bounded timeout waits based on the existing monotonic PIT tick under the single-core cooperative model.

#### Scenario: timeout 未到期前被显式唤醒
- **WHEN** a thread waits with a finite timeout and a producer wakes the wait queue before the deadline
- **THEN** BigOS MUST return a deterministic success result to the waiter
- **AND** the waiter MUST be removed from timeout tracking before it runs again

#### Scenario: timeout 到期唤醒线程
- **WHEN** the monotonic tick reaches or passes a sleeping thread's deadline
- **THEN** BigOS MUST make that thread runnable with a deterministic timeout result
- **AND** the timeout transition MUST NOT require ordinary dynamic allocation in IRQ context

#### Scenario: timeout wait 不改变 tick 所有权
- **WHEN** timeout wait support is enabled
- **THEN** timer tick ownership MUST remain in the timer subsystem through the existing controlled tick API
- **AND** timeout processing MUST NOT require direct mutation of timer-internal tick storage by the scheduler or wait queue layer

### Requirement: 阻塞原语验证可复现
BigOS SHALL validate blocking primitives with source-level checks, build checks, and default-off runtime smoke when the emulator environment is available.

#### Scenario: 源码级检查覆盖等待模型
- **WHEN** this change is implemented
- **THEN** validation MUST include source-level checks for wait state definitions, wait queue enqueue/dequeue, wakeup idempotence, timeout state removal, and forbidden blocking calls from IRQ/timer/keyboard paths

#### Scenario: runtime smoke 观察阻塞与唤醒 marker
- **WHEN** blocking primitives smoke is enabled and QEMU headless serial-marker validation is available
- **THEN** BigOS MUST run a bounded smoke that blocks a kernel thread, wakes it through a deterministic producer or timeout, and emits fixed `BIGOS_` markers showing block, wake, timeout, and completion states

#### Scenario: runtime smoke 不可用时记录风险
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display configuration, serial logging, or disk image generation is unavailable
- **THEN** validation MUST record the missing dependency, substitute source/build checks, skipped smoke cases, and remaining scheduler/timer/IRQ runtime risk

### Requirement: 多队列注册等待支持

BigOS SHALL 扩展调度阻塞原语，使一个非中断内核线程能在同一次阻塞中同时登记到多个 wait queue 上，并在其中任一 wait queue 被唤醒或可选超时到期时被唤醒恰好一次。该多队列等待 MUST 复用既有 monotonic tick 超时机制，MUST 保持登记、注销与唤醒在分配无关、IRQ-safe 的约束内完成，MUST NOT 改变既有单队列 `wait_queue_wait_until`/`wake_one`/`wake_all` 的对外语义，也 MUST NOT 引入抢占式调度、SMP 迁移或用户可见 POSIX 语义。等待线程的多队列登记节点 MUST 来自线程自身的稳定存储，唤醒路径 MUST NOT 依赖普通动态分配。

#### Scenario: 线程同时等待多个 wait queue

- **WHEN** 一个允许阻塞的非中断线程在集合内所有目标条件均不满足时，通过多队列等待原语同时登记到多个 wait queue
- **THEN** BigOS MUST 在使线程非可运行之前原子地记录其对这些 wait queue 的成员关系
- **AND** 调度器 MUST 切换到另一可运行线程或 idle 线程而不破坏 run queue

#### Scenario: 任一队列唤醒使多队列等待线程可运行

- **WHEN** 一个唤醒作用于多队列等待线程所登记的其中一个 wait queue
- **THEN** BigOS MUST 使该线程恰好一次地重新可运行
- **AND** 该线程恢复后 MUST 从其登记的所有 wait queue 注销其等待节点
- **AND** 唤醒 MUST NOT 要求普通动态分配

#### Scenario: 多队列等待超时到期

- **WHEN** 一个多队列等待线程带有限超时且在被任一队列唤醒前 monotonic tick 到达其 deadline
- **THEN** BigOS MUST 以确定性超时结果使该线程重新可运行
- **AND** 超时转换 MUST NOT 在 IRQ 上下文要求普通动态分配

#### Scenario: 多队列等待不改变单队列语义

- **WHEN** 多队列等待支持被引入
- **THEN** 既有单队列 `wait_queue_wait_until`、`wake_one`、`wake_all` 的入队、唤醒幂等与空队列安全行为 MUST 保持不变
- **AND** 对既有单等待线程的唤醒 MUST NOT 被多队列登记节点干扰
