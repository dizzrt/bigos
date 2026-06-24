## ADDED Requirements

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
