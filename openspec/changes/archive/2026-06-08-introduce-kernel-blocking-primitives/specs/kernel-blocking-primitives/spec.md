## ADDED Requirements

### Requirement: 线程等待状态是显式且有界的
BigOS SHALL provide explicit single-core thread wait states for stage 10 blocking primitives, without introducing preemptive scheduling, SMP migration, process ownership, or POSIX blocking policy.

#### Scenario: 线程进入等待状态
- **WHEN** 非中断内核代码把当前线程挂入一个 wait queue 或 sleep queue
- **THEN** BigOS MUST record the thread as blocked, sleeping, or an equivalent bounded non-runnable wait state
- **AND** the thread MUST NOT remain eligible for normal runnable scheduling until a wakeup, timeout, cancellation, or termination transition occurs

#### Scenario: 等待状态保持单核边界
- **WHEN** scheduler or blocking APIs describe stage 10 wait states
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
BigOS SHALL provide bounded timeout waits based on the existing monotonic PIT tick under the stage 10 single-core cooperative model.

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
