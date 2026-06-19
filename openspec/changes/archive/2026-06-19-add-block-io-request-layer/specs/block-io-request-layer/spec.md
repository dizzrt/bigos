## ADDED Requirements

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
