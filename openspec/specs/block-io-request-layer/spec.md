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
BigOS SHALL expose a synchronous block I/O submit path that dispatches queued requests to the currently published block device interface and returns only after the request has completed or failed deterministically. The request layer MUST provide a small set of request-specific statuses for failures before device execution, including invalid request, queue full, and device not ready. For requests that reach the underlying block device, the request layer MUST preserve or normalize deterministic device statuses such as timeout, device error, unsupported operation, and read-only rejection without collapsing them into ambiguous success/failure values. The current implementation MUST NOT claim asynchronous completion, callback delivery, background workers, or scheduler-integrated I/O waiting.

#### Scenario: 同步读请求完成
- **WHEN** an accepted read request is synchronously submitted to a ready block device and the underlying read succeeds
- **THEN** BigOS MUST copy or fill the caller-provided kernel buffer through the block device backend
- **AND** the submit path MUST return a success completion status

#### Scenario: 同步写请求失败
- **WHEN** an accepted write request is synchronously submitted and the underlying block device reports timeout, unsupported write, read-only, or hardware error
- **THEN** BigOS MUST propagate a deterministic failure status to the submitter
- **AND** it MUST NOT report the request as complete-success

#### Scenario: 请求层失败区别于设备失败
- **WHEN** a request fails before device execution because it is invalid, the target device queue is full, or the target device is not ready
- **THEN** BigOS MUST return the corresponding request-layer status
- **AND** it MUST NOT report the failure as a hardware timeout or device write error

### Requirement: 请求层上下文边界
Block I/O request submission SHALL be callable only from ordinary blockable kernel context after device framework publication, port I/O, and memory management are initialized. The request layer MUST NOT be advertised as IRQ-handler-safe, preemption-disabled-safe, scheduler-critical-section-safe, asynchronous, or usable before block devices are published.

#### Scenario: 普通上下文提交请求
- **WHEN** page/buffer cache submits a request from an allowed blockable kernel context after block devices are published
- **THEN** the request layer MAY perform bounded queue bookkeeping and synchronous block device I/O
- **AND** it MUST return an explicit completion status

#### Scenario: 不可阻塞上下文拒绝请求
- **WHEN** an IRQ handler, timer tick, scheduler critical section, preemption-disabled region, or equivalent nonblocking path attempts to submit a block I/O request
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT issue blocking device I/O from that context

### Requirement: 未来异步边界不扩大当前承诺
BigOS SHALL structure request state so future asynchronous dispatch can reuse operation, target, buffer, range, and completion fields, but the current contract MUST remain synchronous. Documentation and diagnostics MUST NOT describe this layer as providing async I/O, background writeback, DMA, multi-queue dispatch, or SMP I/O scheduling.

#### Scenario: 当前提交不返回 pending async 状态
- **WHEN** a caller submits a request through the current request layer
- **THEN** BigOS MUST return a final success or failure status for that synchronous submission
- **AND** it MUST NOT require the caller to wait for a later callback, interrupt, worker, or completion queue

#### Scenario: 文档不声明 async I/O
- **WHEN** implementation notes, validation records, or project documentation describe the request layer
- **THEN** they MUST identify it as a bounded synchronous request layer with future async extension points
- **AND** they MUST NOT claim that BigOS supports async I/O or broad storage scheduling

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
