# block-io-request-layer Specification

## Purpose
TBD - created by archiving change add-block-io-request-layer. Update Purpose after archive.
## Requirements
### Requirement: 有界块 I/O 请求描述
BigOS SHALL provide a freestanding-safe kernel block I/O request descriptor for whole-sector or whole-cache-block read and write operations. The descriptor MUST record target block device, operation type, starting LBA or block number after translation, sector count, kernel buffer, buffer length, completion status, and bounded private bookkeeping without requiring hosted runtime services, exceptions, RTTI, dynamic linking, or uncontrolled global constructors.

#### Scenario: 构造合法读请求
- **WHEN** page/buffer cache 或内核存储路径构造一个目标设备有效、范围非溢出、缓冲区足够大的读请求
- **THEN** BigOS MUST accept the request descriptor for submission
- **AND** the descriptor MUST preserve enough information for deterministic execution and completion status reporting

#### Scenario: 拒绝非法请求描述
- **WHEN** a request has no target block device, an unsupported operation type, zero or invalid sector count, overflowing range arithmetic, or an undersized buffer
- **THEN** BigOS MUST reject the request before queueing or issuing device I/O
- **AND** it MUST return a deterministic validation error

### Requirement: 有界请求队列
BigOS SHALL provide a bounded block I/O request queue with deterministic per-device capacity for each published block device used through the request layer. Each queue MUST preserve submission order for accepted synchronous requests targeting that device, reject capacity exhaustion deterministically, and MUST NOT allocate unbounded memory or spin indefinitely when full.

#### Scenario: 请求入队成功
- **WHEN** a valid request is submitted while the bounded queue has an available slot
- **THEN** BigOS MUST enqueue the request for synchronous dispatch
- **AND** the request MUST later complete with either success or a deterministic device/error status

#### Scenario: 队列容量耗尽
- **WHEN** a valid request is submitted while the bounded queue has no available slot
- **THEN** BigOS MUST reject the request with a deterministic queue-full status
- **AND** it MUST NOT issue partial device I/O for that rejected request

#### Scenario: 不同设备队列相互隔离
- **WHEN** one published block device has exhausted its request queue and another published block device still has an available request slot
- **THEN** BigOS MUST reject new requests for the exhausted device with a queue-full status
- **AND** it MUST still allow valid requests for the other device to be submitted

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

### Requirement: 请求层 completion 身份与队列槽匹配
BigOS SHALL bind each pending completion to the accepted request and its bounded queue ownership. Completion MUST target the intended request identity and MUST NOT complete an unrelated request that reuses a queue slot after timeout, cancellation, or prior completion.

#### Scenario: completion 匹配 pending 请求
- **WHEN** a completion entry is called with the identity of an accepted pending request
- **THEN** the request layer MUST verify that the request is still pending in the expected queue context
- **AND** it MUST apply the final status only to that request

#### Scenario: 迟到 completion 不污染新请求
- **WHEN** a completion arrives for a request after timeout or cancellation and the same queue slot has been reused by another request
- **THEN** BigOS MUST reject or diagnose the stale completion
- **AND** it MUST NOT modify the newer request's status or wake queue

### Requirement: 请求层验证覆盖中断完成路径
BigOS SHALL extend block I/O request-layer validation to cover interrupt-driven completion behavior in addition to existing synchronous read/write, validation failure, queue exhaustion, and device error propagation.

#### Scenario: 验证 pending 到完成
- **WHEN** request-layer validation enables the interrupt-driven completion case
- **THEN** it MUST submit or simulate a request that enters pending state
- **AND** it MUST complete that request through the bounded completion entry and observe the expected final status

#### Scenario: 验证 forbidden context
- **WHEN** source-level or runtime validation checks request-layer context boundaries
- **THEN** it MUST cover that request submission and waiting remain forbidden from IRQ, timer, scheduler critical, preemption-disabled, or equivalent nonblocking paths
- **AND** it MUST distinguish the IRQ-safe completion entry from forbidden submission or waiting APIs

### Requirement: 请求层验证可复现
BigOS SHALL provide deterministic validation for the block I/O request layer through source-level checks and default-off runtime smoke coverage where emulator and toolchain availability permits. Validation MUST cover successful synchronous read/write submission, request validation failure, queue capacity exhaustion, and device error propagation.

#### Scenario: smoke 覆盖请求成功与失败
- **WHEN** the request-layer validation path is enabled in an emulator environment with the expected toolchain and disk image support
- **THEN** validation MUST exercise at least one successful read or write request and at least one deterministic rejected or failed request
- **AND** it MUST emit bounded diagnostic markers or logs according to the existing smoke style

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display dependencies, or disk image setup required by runtime validation are unavailable
- **THEN** validation notes MUST record the skipped coverage and residual risk
- **AND** they MUST NOT claim runtime smoke success for the skipped environment

### Requirement: 请求层支持第二块后端
BigOS SHALL allow the block I/O request layer to submit synchronous read and write requests to a second published block-device backend in addition to the existing ATA-backed devices. The request layer MUST use the same request validation, queueing, synchronous completion, and deterministic status propagation for the RAM block backend as for other published block devices.

#### Scenario: 对 RAM 后端同步读写成功
- **WHEN** a valid write request and later valid read request are submitted to the published RAM block backend
- **THEN** the request layer MUST dispatch both operations through the RAM backend `BlockDevice` interface
- **AND** it MUST return success only after each synchronous operation has completed

#### Scenario: RAM 后端请求校验失败
- **WHEN** a request to the RAM block backend has no device, an invalid operation, zero sector count, overflowing LBA range, or an undersized buffer
- **THEN** the request layer MUST reject the request before backend execution
- **AND** it MUST return the corresponding deterministic request-layer status

#### Scenario: 未发布 RAM 后端不可提交
- **WHEN** validation or kernel code attempts to submit a request for the RAM block role before framework publication succeeds
- **THEN** the request layer consumer MUST observe a deterministic not-ready or not-found status
- **AND** it MUST NOT bypass the device framework by constructing an unrelated backend

### Requirement: 多块设备队列隔离可验证
BigOS SHALL preserve per-device bounded queue isolation when both an ATA-backed block device and the RAM block backend are present. Queue capacity exhaustion or failure for one device MUST NOT consume queue slots for another published block device or cause requests to dispatch to the wrong backend.

#### Scenario: RAM 队列满不影响 ATA 队列
- **WHEN** validation fills or simulates exhaustion of the RAM block backend request queue while an ATA-backed block device queue still has capacity
- **THEN** new requests for the RAM backend MUST fail with a deterministic queue-full status
- **AND** valid requests for the ATA-backed device MUST still be accepted according to its own queue state

#### Scenario: ATA 队列满不影响 RAM 队列
- **WHEN** validation fills or simulates exhaustion of an ATA-backed block device request queue while the RAM block backend queue still has capacity
- **THEN** new requests for the ATA-backed device MUST fail with a deterministic queue-full status
- **AND** valid requests for the RAM backend MUST still be accepted according to its own queue state

### Requirement: cache 可使用第二后端
BigOS SHALL allow page/buffer cache validation to use the published RAM block backend through the block I/O request layer. Cache load, dirty update, explicit sync, and later readback against the RAM backend MUST preserve existing dirty-state and error semantics.

#### Scenario: cache 经 RAM 后端读写往返
- **WHEN** validation obtains a cache block for the RAM block backend, modifies it, marks it dirty, synchronizes it, and then reads the same block again
- **THEN** the cache path MUST submit backend I/O through the block request layer
- **AND** the later read MUST observe the latest successfully synchronized contents

#### Scenario: RAM 后端写回失败保留 dirty 状态
- **WHEN** a cache writeback to the RAM block backend fails because the request is rejected or the backend reports a deterministic error
- **THEN** BigOS MUST preserve the cache block's dirty or failure state according to the existing cache contract
- **AND** it MUST NOT report synchronization success or reuse the block as clean data

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

### Requirement: 请求层发布统一 terminal 原因
块 I/O 请求层 MUST 为同步 wrapper 和内部消费者发布统一 terminal reason，至少能区分 success、invalid request、queue full、issue failure、device error、timeout、cancel 或等价 completion rejection；调用者不得需要解析设备私有状态才能判断最终结果。

#### Scenario: 同步 wrapper 观察设备错误
- **WHEN** 设备完成源以 device error 结束一个已 pending 请求
- **THEN** 请求层 MUST 将该请求转为 terminal device error，并让同步提交调用返回确定性失败而不是 success 或无限等待

#### Scenario: queue full 可诊断
- **WHEN** 请求层无法为新请求分配队列槽位
- **THEN** 请求层 MUST 返回 queue full 或等价有界失败状态，且不得构造未 armed 的悬空 token

### Requirement: 槽位释放只发生在 terminal 观察后
请求队列槽位 MUST 只在请求达到 terminal 状态且等待者或同步 wrapper 已完成最终状态观察后进入可复用状态；槽位复用 MUST 更新 generation 或等价身份，防止旧 completion 影响新请求。

#### Scenario: 槽位复用生成新身份
- **WHEN** 一个 terminal 请求释放槽位后，新请求复用同一槽位
- **THEN** 请求层 MUST 为新请求生成新的可验证身份，使旧 token 的 completion 被拒绝

### Requirement: 请求层上下文边界覆盖生命周期错误
请求层 MUST 明确区分可阻塞提交/等待路径与 IRQ-safe completion 路径；生命周期错误处理 MUST 保持在允许的上下文内，IRQ-safe 路径不得执行分配、释放、阻塞等待、cache policy、filesystem policy 或用户内存访问。

#### Scenario: IRQ 完成唤醒等待者
- **WHEN** IRQ-safe completion entry 合法完成一个 pending 请求
- **THEN** 请求层 MUST 只发布 terminal 状态、更新有界诊断并唤醒等待者，不得在该路径执行同步 wrapper 的后续清理、cache writeback 或文件系统提交

### Requirement: 请求层诊断可区分生命周期失败
请求层 MUST 提供默认关闭验证可观察的有界诊断，用于区分 issue failure、timeout、cancel、device error、late completion、duplicate completion、identity mismatch 和 slot reuse protection。

#### Scenario: 验证读取诊断快照
- **WHEN** 默认关闭块 I/O 验证触发 timeout 后迟到 completion
- **THEN** 请求层诊断 MUST 能显示 timeout terminal 已发布且迟到 completion 被拒绝，验证不得只依赖最终返回码

