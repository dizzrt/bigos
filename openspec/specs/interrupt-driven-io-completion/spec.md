# interrupt-driven-io-completion Specification

## Purpose

Define the bounded kernel-internal interrupt-driven completion model for block I/O requests, including completion state monotonicity, IRQ-safe completion boundaries, scheduler wakeup integration, and reproducible validation.
## Requirements
### Requirement: I/O completion 状态有界且单调
BigOS SHALL provide a freestanding-safe I/O completion state for kernel block requests. The completion state MUST represent pending, completed-success, completed-error, timeout-or-cancelled, and invalid states deterministically, and a request MUST transition to a terminal completion state at most once.

#### Scenario: 请求进入 pending 状态
- **WHEN** a valid block request is accepted for interrupt-driven completion
- **THEN** BigOS MUST record the request as pending before returning control to a waiting submitter or issuing device work
- **AND** the pending state MUST include enough bounded bookkeeping to report the final request-layer status

#### Scenario: 完成状态只记录一次
- **WHEN** a pending request is completed with success or a deterministic error
- **THEN** BigOS MUST atomically transition the request to the corresponding terminal state
- **AND** a later attempt to complete the same request MUST NOT overwrite the first terminal status

#### Scenario: timeout 与完成竞争可诊断
- **WHEN** a waiting request times out or is cancelled before the device completion arrives
- **THEN** BigOS MUST record a deterministic timeout-or-cancelled state for the waiter
- **AND** a later device completion MUST be rejected or recorded as late without waking the same waiter a second time

### Requirement: IRQ-safe 完成入口只做有界工作
BigOS SHALL expose a bounded completion entry that may be called from an eligible storage IRQ handler or IRQ-like validation producer. The completion entry MUST NOT allocate memory, free memory, block, submit new filesystem or cache I/O, access user memory, depend on hosted runtime services, or send irqchip EOI directly.

#### Scenario: IRQ handler 完成 pending 请求
- **WHEN** an eligible IRQ handler observes that a previously issued block request has completed
- **THEN** it MAY call the completion entry with the request identity and final deterministic status
- **AND** the completion entry MUST update only bounded request/completion state and scheduler wakeup state

#### Scenario: 完成入口不拥有 EOI
- **WHEN** the completion entry is invoked from an external IRQ path
- **THEN** BigOS MUST leave PIC/LAPIC EOI ownership with the existing interrupt dispatch or irqchip-specific owner
- **AND** the completion entry MUST NOT send PIC EOI, LAPIC EOI, or alter syscall/exception dispatch behavior

#### Scenario: 非 pending 请求完成被拒绝
- **WHEN** completion is requested for an invalid, unqueued, already terminal, or unrelated request
- **THEN** BigOS MUST reject the completion deterministically
- **AND** it MUST NOT corrupt another request's completion state or wake queue

### Requirement: 等待线程通过 scheduler wakeup 恢复
BigOS SHALL integrate I/O completion with the existing scheduler wait queue model so ordinary blockable kernel threads can wait for request completion, timeout, or deterministic failure. Wakeup from completion MUST be allocation-free and MUST make the waiter runnable exactly once.

#### Scenario: 等待线程被完成唤醒
- **WHEN** a blockable kernel thread waits on a pending I/O request and the completion entry records a terminal success or error
- **THEN** BigOS MUST wake the waiting thread through the scheduler wait queue boundary
- **AND** the resumed waiter MUST observe the final request-layer status

#### Scenario: 等待线程 timeout
- **WHEN** a blockable kernel thread waits on a pending I/O request with a finite timeout and no completion arrives before the deadline
- **THEN** BigOS MUST resume the waiter with a deterministic timeout status
- **AND** the request MUST no longer be reported as successful to that waiter

#### Scenario: 不可阻塞上下文不能等待 I/O
- **WHEN** an IRQ handler, timer hook, scheduler critical section, preemption-disabled region, CPU exception path, or equivalent nonblocking context attempts to wait for I/O completion
- **THEN** BigOS MUST reject the wait or enter a documented diagnostic path
- **AND** it MUST NOT enqueue a nonblocking context on a scheduler wait queue

### Requirement: 完成诊断可复现
BigOS SHALL provide deterministic diagnostics and validation coverage for interrupt-driven I/O completion. Validation MUST cover pending requests, completion wakeup, repeated completion rejection, timeout behavior, error propagation, and forbidden blocking contexts.

#### Scenario: 默认关闭 smoke 覆盖完成闭环
- **WHEN** interrupt-driven I/O completion validation is enabled in an emulator environment with the expected toolchain and disk image support
- **THEN** validation MUST exercise at least one request that enters pending state and is completed by an IRQ-like producer
- **AND** it MUST observe that the waiting thread wakes with the expected final status

#### Scenario: 重复完成和 timeout 被覆盖
- **WHEN** validation runs the completion edge-case cases
- **THEN** it MUST cover repeated completion rejection and timeout-before-completion behavior
- **AND** it MUST report deterministic pass/fail diagnostics according to the existing default-off smoke style

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display dependencies, serial logging, or disk image setup required by runtime validation are unavailable
- **THEN** validation notes MUST record the skipped completion coverage and residual risk
- **AND** they MUST NOT claim runtime smoke success for the skipped environment

### Requirement: completion 模型承载真实块路径
BigOS SHALL allow the interrupt-driven I/O completion model to serve the migrated kernel block path, not only validation producers. A real storage driver or equivalent bounded completion source MUST be able to complete an issued request by using the request identity and final deterministic status, while preserving the existing allocation-free, nonblocking, EOI-neutral completion-entry contract.

#### Scenario: 真实设备完成 pending 请求
- **WHEN** a migrated storage backend observes that an issued pending request has completed
- **THEN** it MUST complete that request through the bounded completion entry with a deterministic final status
- **AND** the waiting submitter MUST observe the same final request-layer status

#### Scenario: 完成入口仍不拥有 EOI
- **WHEN** a storage IRQ path uses the completion entry for a migrated block request
- **THEN** BigOS MUST leave PIC/LAPIC EOI ownership with the interrupt dispatch or irqchip-specific owner
- **AND** the completion entry MUST NOT send PIC EOI, LAPIC EOI, or alter syscall/exception dispatch behavior

#### Scenario: completion 不执行后续 I/O 策略
- **WHEN** a migrated request completes from IRQ context or an IRQ-like producer
- **THEN** the completion path MUST update bounded state and wake waiters only
- **AND** it MUST NOT submit another request, perform cache writeback, access filesystems, allocate memory, free memory, or block

### Requirement: timeout 与真实设备迟到完成可诊断
BigOS SHALL keep timeout, cancellation, and late-completion behavior deterministic when the completion source is a real device path. A request that times out or is cancelled before device completion MUST transition to a terminal timeout-or-cancelled state for the waiter, and a later device completion MUST be rejected or diagnosed without waking the same waiter again.

#### Scenario: 真实设备完成晚于 timeout
- **WHEN** a waiting block request times out before the storage completion source reports completion
- **THEN** BigOS MUST return pending-timeout or an equivalent deterministic timeout status to the waiting submitter
- **AND** the later device completion MUST NOT overwrite the timeout status or complete a reused queue slot

#### Scenario: cancel 后 completion 被拒绝
- **WHEN** a pending request is cancelled and the device later reports completion for its old token
- **THEN** BigOS MUST reject or diagnose the completion
- **AND** it MUST NOT wake the cancelled waiter a second time

### Requirement: 完成诊断覆盖迁移后的块路径
BigOS SHALL extend completion validation so migrated block requests are covered in addition to fake or IRQ-like producers. Validation MUST show that a request issued through the normal block layer can enter pending state, be completed through the completion entry, wake a synchronous wrapper, and preserve final status mapping.

#### Scenario: 正常块请求完成闭环被验证
- **WHEN** nonpolling block-path validation is enabled in an emulator environment with the expected toolchain and disk image support
- **THEN** validation MUST issue at least one normal block request that reaches pending state
- **AND** it MUST complete that request through the completion boundary and observe the synchronous caller resume with the expected status

#### Scenario: 错误和 timeout 被验证
- **WHEN** migrated completion validation runs edge cases
- **THEN** it MUST cover device error propagation, request timeout, repeated completion rejection, and stale completion rejection
- **AND** it MUST report deterministic pass/fail diagnostics according to the existing default-off smoke style

