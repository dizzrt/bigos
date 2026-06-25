## ADDED Requirements

### Requirement: 用户可见阻塞式 sleep syscall
BigOS SHALL expose a bounded user-visible blocking sleep syscall that lets the current user process yield the CPU until a coarse monotonic tick deadline expires. The syscall MUST use the existing `int 0x80` register ABI, append-only syscall numbering, and negative errno return convention. It MUST NOT change `VECTOR_SYSCALL = 0x80`, existing syscall numbers, syscall gate privilege, exception/IRQ EOI rules, boot layout, page-table layout, CR3 switching, or disk image layout.

#### Scenario: 零毫秒 sleep 立即成功
- **WHEN** a user process invokes `SYS_SLEEP_MS` with `milliseconds == 0`
- **THEN** BigOS MUST return 0 without enqueuing the current thread on the scheduler sleep list
- **AND** it MUST NOT allocate memory, mutate fd state, mutate process identity, or enter a busy wait

#### Scenario: 非零 sleep 阻塞到 tick deadline
- **WHEN** a user process invokes `SYS_SLEEP_MS` with a supported nonzero millisecond duration from ordinary syscall context
- **THEN** BigOS MUST convert the duration to a monotonic tick deadline by rounding up to at least one tick
- **AND** it MUST block the current scheduler thread through the existing scheduler/timer sleep primitive until the deadline expires
- **AND** it MUST return 0 after the sleeping thread becomes runnable because the timeout expired

#### Scenario: syscall ABI 追加而不改号
- **WHEN** the blocking sleep syscall is added
- **THEN** BigOS MUST append a new syscall number after the existing syscall surface or use a documented unused entry
- **AND** all existing syscall numbers, argument register order, return register behavior, and syscall no-EOI semantics MUST remain unchanged

### Requirement: sleep syscall 参数与错误语义有界
BigOS SHALL validate sleep syscall arguments before scheduling away the current thread. The syscall MUST reject values that cannot be safely converted to a scheduler tick deadline and MUST map scheduler-internal wait results to POSIX-style negative errno values rather than exposing scheduler private constants to user mode.

#### Scenario: 毫秒到 tick 转换向上取整
- **WHEN** `SYS_SLEEP_MS` receives a supported nonzero millisecond value smaller than one PIT tick
- **THEN** BigOS MUST convert the request to one scheduler tick
- **AND** the syscall MUST NOT treat the request as a zero-duration no-op

#### Scenario: 超界或溢出参数失败
- **WHEN** `SYS_SLEEP_MS` receives a millisecond value that exceeds the documented bounded maximum derived from the current tick deadline safety margin or would overflow during millisecond-to-tick or deadline conversion
- **THEN** BigOS MUST fail with a deterministic negative errno such as `-EINVAL`
- **AND** it MUST NOT enqueue the current thread, publish a partial sleep state, or wrap the deadline

#### Scenario: 不可阻塞上下文失败
- **WHEN** `SYS_SLEEP_MS` is reached from a context where scheduler blocking is forbidden
- **THEN** BigOS MUST fail with a deterministic negative errno such as `-EWOULDBLOCK`
- **AND** it MUST NOT silently enqueue the current thread, spin in place as a substitute for sleep, or corrupt scheduler state

### Requirement: sleep syscall 复用现有 scheduler/timer 边界
BigOS SHALL implement blocking sleep by reusing the existing monotonic tick and scheduler sleep list boundary. The implementation MUST keep timer IRQ handling allocation-free and MUST NOT introduce a separate user timer queue, process timer object model, or hosted runtime dependency.

#### Scenario: timer IRQ 路径保持 IRQ-safe
- **WHEN** PIT IRQ0 advances ticks while one or more user sleep syscalls are pending
- **THEN** timeout wakeup processing MUST remain bounded and allocation-free in IRQ context
- **AND** the timer handler MUST NOT directly send i8259 EOI, perform filesystem/block I/O, fault in user memory, or call hosted runtime APIs

#### Scenario: scheduler sleep list 管理 runnable transition
- **WHEN** the sleep deadline expires
- **THEN** the scheduler MUST make the sleeping thread runnable exactly once through its existing timeout wakeup path
- **AND** stale sleep-list membership or duplicate run-queue publication MUST be prevented or handled by the existing scheduler invariants

### Requirement: sleep syscall 不声明完整 POSIX timer 语义
BigOS SHALL document the blocking sleep syscall as a bounded coarse tick-based sleep primitive. The syscall and libc wrappers MUST NOT imply complete POSIX timer, signal-interruptible sleep, high-resolution clock, async timer, alarm, timerfd, or realtime scheduling semantics.

#### Scenario: signal pending 不定义为 sleep 中断
- **WHEN** a signal becomes pending for a process that is blocked in `SYS_SLEEP_MS`
- **THEN** BigOS MAY deliver the signal at an existing user return boundary after the sleeping thread resumes
- **AND** the sleep syscall MUST NOT be required to wake early, return `-EINTR`, or write remaining time as part of this change

#### Scenario: 文档保持 bounded 描述
- **WHEN** headers, docs, OpenSpec artifacts, or user examples describe sleep support
- **THEN** they MUST describe it as coarse tick-based bounded sleep
- **AND** they MUST NOT claim `nanosleep`, `clock_nanosleep`, `usleep`, `alarm`, timerfd, async timer, high-resolution sleep, or complete POSIX `sleep` compatibility

### Requirement: sleep syscall 用户态验证可复现
BigOS SHALL provide default-off validation for the blocking sleep syscall through a user-visible runtime smoke and source-level contract checks. Runtime validation MUST prefer QEMU headless serial-marker checks when the local toolchain and emulator dependencies are available, and skipped runtime validation MUST be recorded explicitly.

#### Scenario: user smoke 观察 tick 下界
- **WHEN** the blocking sleep syscall smoke runs in a configured emulator environment
- **THEN** a user program MUST read the monotonic tick before and after a bounded `bigos_sleep_ms()` call
- **AND** it MUST pass only when the observed tick delta is at least the requested duration rounded up to ticks
- **AND** it MUST emit deterministic success or failure markers such as `BIGOS_SLEEP_SYSCALL_PASSED` or `BIGOS_SLEEP_SYSCALL_FAILED`

#### Scenario: source checks 覆盖 ABI 与边界
- **WHEN** this change is implemented
- **THEN** validation MUST include source-level checks for syscall number append-only behavior, libc syscall number mirroring, wrapper declarations, millisecond-to-tick bounds, forbidden blocking context handling, and marker registration
- **AND** those checks MUST distinguish current-change failures from pre-existing diagnostics

#### Scenario: runtime 依赖不可用时记录跳过
- **WHEN** x86_64 cross toolchain, xmake, QEMU, Bochs, ROM/display configuration, generated image paths, or serial marker oracle are unavailable
- **THEN** validation notes MUST record the missing dependency, substitute checks that ran, skipped smoke cases, and residual syscall/timer/scheduler runtime risk
