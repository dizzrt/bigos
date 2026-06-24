## MODIFIED Requirements

### Requirement: 同步提交与完成状态
BigOS SHALL preserve the existing synchronous block I/O submit API for current consumers while making it a bounded wrapper over the request-layer issue, pending completion, scheduler wait, and final-status path. Accepted requests MAY complete immediately only through the request-layer completion/status machinery, or later through the bounded interrupt-driven completion model. The request layer MUST provide a small set of request-specific statuses for failures before device execution, including invalid request, queue full, device not ready, would block, pending timeout, and completion rejection. For requests that reach the underlying block device or interrupt-driven completion path, the request layer MUST preserve or normalize deterministic device statuses such as timeout, device error, unsupported operation, read-only rejection, and short read without collapsing them into ambiguous success/failure values. Existing synchronous consumers MUST still receive a final success or failure status before `submit_sync` returns, but the request layer MUST NOT depend on a block-device synchronous polling implementation to produce whole-request completion in the submit call stack.

#### Scenario: 同步读请求经 completion 后完成
- **WHEN** an accepted read request is synchronously submitted to a ready block device and the underlying device path completes the request successfully
- **THEN** BigOS MUST copy or fill the caller-provided kernel buffer through the block device backend
- **AND** the submit path MUST return a success completion status only after the request-layer completion state reaches terminal success

#### Scenario: pending 请求等待后完成
- **WHEN** an accepted request is issued through an interrupt-driven-capable path and enters pending state
- **THEN** `submit_sync` or an equivalent synchronous wrapper MUST wait only from an allowed blockable kernel context
- **AND** it MUST return the final success, timeout, or deterministic failure status after completion or timeout

#### Scenario: 同步写请求失败
- **WHEN** an accepted write request is synchronously submitted and the underlying block device or completion path reports timeout, unsupported write, read-only, or hardware error
- **THEN** BigOS MUST propagate a deterministic failure status to the submitter
- **AND** it MUST NOT report the request as complete-success

#### Scenario: 请求层失败区别于设备失败
- **WHEN** a request fails before device execution because it is invalid, the target device queue is full, the target device is not ready, completion state cannot be armed, or the device issue path rejects the request
- **THEN** BigOS MUST return the corresponding request-layer status
- **AND** it MUST NOT report the failure as a hardware timeout or device write error

### Requirement: 请求层上下文边界
Block I/O request submission SHALL be callable only from ordinary blockable kernel context after device framework publication, port I/O, and memory management are initialized. Waiting for request completion MUST also require an ordinary blockable kernel context. The request layer MAY expose a separate bounded completion entry for eligible IRQ handlers, device completion sources, or IRQ-like validation producers, but that entry MUST only update completion state and wake waiters; it MUST NOT submit requests, synchronously poll for whole-request completion, block, allocate, free, access filesystems, or perform cache writeback from IRQ context.

#### Scenario: 普通上下文提交请求
- **WHEN** page/buffer cache submits a request from an allowed blockable kernel context after block devices are published
- **THEN** the request layer MAY perform bounded queue bookkeeping and issue synchronous-compatible or interrupt-driven block device work
- **AND** it MUST return an explicit final status to synchronous callers through the request-layer completion/status path

#### Scenario: 不可阻塞上下文拒绝提交或等待
- **WHEN** an IRQ handler, timer tick, scheduler critical section, preemption-disabled region, or equivalent nonblocking path attempts to submit a block I/O request or wait for request completion
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT issue blocking device I/O or enqueue a waiter from that context

#### Scenario: IRQ 上下文只允许完成 pending 请求
- **WHEN** an eligible storage IRQ handler or IRQ-like validation producer observes completion for an already pending request
- **THEN** it MAY call the request-layer completion entry with a deterministic final status
- **AND** the completion entry MUST NOT perform request submission, synchronous polling, cache operations, filesystem operations, or irqchip EOI

### Requirement: 未来异步边界不扩大当前承诺
BigOS SHALL structure request state so interrupt-driven completion can reuse operation, target, buffer, range, queue, wait, and completion fields while the current public consumer contract remains bounded and kernel-internal. Documentation and diagnostics MAY describe the request layer as supporting scheduler-integrated interrupt-driven completion for kernel block requests and as no longer depending on whole-request synchronous polling for the migrated block path, but they MUST NOT describe BigOS as providing complete async I/O, user-visible async syscalls, background writeback, DMA, multi-queue dispatch, broad storage scheduling, or modern storage driver support unless later changes explicitly add those capabilities.

#### Scenario: 当前同步提交仍返回最终状态
- **WHEN** a caller submits a request through the existing synchronous request-layer API
- **THEN** BigOS MUST return a final success, timeout, or deterministic failure status for that synchronous submission
- **AND** the caller MUST NOT be required to handle a later callback after the synchronous API returns

#### Scenario: pending 状态不等于完整 async I/O
- **WHEN** a request internally enters pending state and is later completed through the completion entry
- **THEN** BigOS MAY wait through scheduler primitives and return the final status to the synchronous caller
- **AND** it MUST NOT expose this as a user-visible async I/O ABI or complete asynchronous storage scheduler

#### Scenario: 文档限定非轮询完成范围
- **WHEN** implementation notes, validation records, or project documentation describe the request layer after this change
- **THEN** they MUST identify it as a bounded kernel-internal nonpolling completion path for block requests
- **AND** they MUST NOT claim DMA, virtio, AHCI/SATA, NVMe, broad networking, background worker writeback, or complete async I/O support

## ADDED Requirements

### Requirement: 请求层 issue 与 completion 身份绑定
BigOS SHALL bind each device issue attempt to the accepted request, queue slot, device identity, and completion generation before the device can report success. A completion from a device IRQ or equivalent source MUST target only the request identity that was issued and MUST NOT complete an unrelated request that reuses a queue slot after timeout, cancellation, or prior completion.

#### Scenario: issue 后 completion 匹配请求
- **WHEN** a device completion source completes an accepted pending request
- **THEN** the request layer MUST verify the request pointer, device identity, queue slot, and generation
- **AND** it MUST apply the final status only to that request

#### Scenario: issue 失败释放队列槽
- **WHEN** a request is accepted by the queue but device issue fails before the request can be pending
- **THEN** BigOS MUST release the queue slot deterministically
- **AND** it MUST return the issue failure without leaving stale pending state

#### Scenario: 迟到 completion 不污染新请求
- **WHEN** a completion arrives for a request after timeout or cancellation and the same queue slot has been reused by another request
- **THEN** BigOS MUST reject or diagnose the stale completion
- **AND** it MUST NOT modify the newer request's status or wake queue
