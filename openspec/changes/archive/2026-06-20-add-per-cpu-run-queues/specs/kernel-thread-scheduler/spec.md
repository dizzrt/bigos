## MODIFIED Requirements

### Requirement: Scheduler uses single-core round-robin policy

BigOS SHALL provide a bounded round-robin scheduler that selects runnable kernel threads from CPU-owned run queues, rotates the current thread through explicit cooperative yield, and may preempt an eligible running thread on a timer-driven IRQ-return boundary after its time slice expires. With only the bootstrap CPU online, this behavior MUST preserve the existing single-core round-robin semantics; with scheduler SMP enabled, the same policy applies independently within each online CPU scheduler domain.

#### Scenario: Yield switches to another runnable thread

- **WHEN** a running kernel thread calls `yield()` and at least one other runnable non-idle thread exists on the current CPU run queue
- **THEN** the scheduler MUST place the current thread back on that CPU's runnable queue
- **AND** the scheduler MUST switch to the next runnable thread in deterministic round-robin order for that CPU

#### Scenario: Yield returns when no peer is runnable

- **WHEN** a running kernel thread calls `yield()` and no other non-idle runnable thread exists on the current CPU run queue
- **THEN** the scheduler MUST continue running the current thread or switch to that CPU's idle thread without corrupting any run queue

#### Scenario: Scheduler supports bounded SMP run queues

- **WHEN** scheduler APIs or documentation describe scheduling behavior after per-CPU run queues are enabled
- **THEN** they MUST state that BigOS provides bounded per-CPU run queues, CPU-local current and idle ownership, and controlled cross-CPU wakeup
- **AND** they MUST NOT claim CPU hotplug, NUMA, complete load balancing, POSIX scheduling policy, cross-CPU TLB shootdown, or complete APIC default interrupt delivery

#### Scenario: Timer slice expiry requests preemption

- **WHEN** a CPU-local timer observes that the current ordinary runnable thread has consumed its configured time slice
- **THEN** BigOS MUST record a bounded reschedule intent in that CPU's scheduler domain without ordinary dynamic allocation
- **AND** the scheduler MAY switch away from the current thread only at a documented IRQ-return scheduling boundary where preemption is enabled

#### Scenario: Preemption preserves cooperative scheduling

- **WHEN** no timer-driven preemption is pending or preemption is disabled
- **THEN** explicit `yield()`, blocking wait, timeout wakeup, and idle scheduling MUST continue to use the existing scheduler state machine through CPU-owned scheduler domains
- **AND** runnable queue order MUST remain deterministic under the bounded round-robin policy

### Requirement: Scheduler accounts time slices
BigOS SHALL track a bounded per-thread or per-current-thread time slice under each online CPU scheduler domain and use that CPU's local scheduler tick to decide when an eligible running thread should be rescheduled.

#### Scenario: Runnable thread receives a time slice
- **WHEN** the scheduler selects an ordinary runnable non-idle thread on any online CPU
- **THEN** BigOS MUST assign or refresh a bounded time slice for that thread
- **AND** the time slice state MUST NOT require ordinary dynamic allocation during timer IRQ handling

#### Scenario: Timer tick consumes current slice
- **WHEN** a CPU-local timer interrupt runs while an ordinary runnable thread is current and preemption accounting is enabled
- **THEN** BigOS MUST decrement or otherwise account that thread's time slice through an IRQ-safe scheduler hook for the current CPU
- **AND** the hook MUST NOT allocate, free, block, call `mdelay()`, access filesystem services, depend on user-mode services, or perform bulk console/serial output

#### Scenario: Idle thread is not pointlessly preempted
- **WHEN** a CPU's idle thread is current and no non-idle runnable thread exists on that CPU run queue
- **THEN** timer tick accounting MUST NOT force a meaningless preemptive switch away from idle
- **AND** idle MUST remain scheduler-owned and may halt only under documented interrupt-readiness assumptions

### Requirement: Scheduler consumes context boundary semantics

BigOS scheduler SHALL consume context-switch and interrupt-return semantics through documented scheduler-facing boundaries rather than depending on raw x86_64 ISR frame or assembly implementation details.

#### Scenario: Scheduler requests switch through context primitive

- **WHEN** the scheduler selects another runnable kernel thread on the current CPU
- **THEN** it MUST transfer control through the documented kernel context-switch primitive or a scheduler-owned wrapper around it
- **AND** ordinary scheduler policy MUST NOT open-code callee-saved register layout, stack-frame construction, or architecture-private assembly offsets outside the context boundary

#### Scenario: IRQ-return preemption uses eligibility checks

- **WHEN** timer-driven or scheduler-nudge preemption intent is processed at an IRQ-return boundary
- **THEN** the scheduler MUST verify current CPU ownership, preemption state, current thread eligibility, run-queue state, and non-preemptible region guards before switching
- **AND** the switch MUST be deferred when the interrupted path cannot safely preserve scheduler and interrupt frame semantics

#### Scenario: Context switch publishes CPU-local ownership

- **WHEN** a CPU switches from one kernel thread to another
- **THEN** BigOS MUST publish the new CPU-local current thread, process, address-space root, TSS/RSP0 ownership, and reschedule state through the documented scheduler/per-CPU boundary
- **AND** publication MUST preserve the existing context-switch ABI and user-mode entry ABI
