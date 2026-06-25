## ADDED Requirements

### Requirement: 请求层支持现代块存储后端
BigOS SHALL allow the block I/O request layer to submit bounded read and write requests to a published modern block-storage backend using the same validation, queue ownership, issue, wait, completion, and terminal-status semantics used for other supported block backends.

#### Scenario: 现代后端请求成功完成
- **WHEN** a valid read or write request targets a ready modern block-storage backend from an allowed blockable kernel context
- **THEN** the request layer MUST validate and enqueue the request, issue it to the modern backend, and return success only after terminal completion success
- **AND** the caller MUST NOT need to inspect modern-driver private status fields to determine whether the operation succeeded

#### Scenario: 现代后端请求失败传播确定性状态
- **WHEN** a modern-backend request fails because of invalid parameters, queue exhaustion, issue failure, device-not-ready, timeout, completion rejection, or device error
- **THEN** the request layer MUST return the corresponding deterministic request-layer or device status
- **AND** it MUST NOT collapse those failures into ambiguous success or an infinite wait

### Requirement: 现代后端队列与其他后端隔离
BigOS SHALL keep request queue ownership and capacity for the modern block-storage backend isolated from ATA, RAM, and any other published block backend. Failure, timeout, or capacity exhaustion on one backend MUST NOT consume slots or complete requests for another backend.

#### Scenario: 现代队列满不影响 ATA 或 RAM
- **WHEN** the modern backend request queue is exhausted while an ATA-backed or RAM-backed queue still has capacity
- **THEN** new requests for the modern backend MUST fail with queue-full or an equivalent deterministic status
- **AND** valid requests for the other backend MUST still be accepted according to that backend's own queue state

#### Scenario: 其他后端失败不污染现代请求
- **WHEN** an ATA-backed or RAM-backed request times out, fails, or exhausts its queue while a modern-backend request is pending
- **THEN** the modern-backend request MUST retain its own device identity, queue slot, generation, and completion state
- **AND** completion for the other backend MUST NOT modify or wake the modern-backend request

### Requirement: 现代后端 completion 保持请求层身份绑定
BigOS SHALL bind each modern-backend issue attempt to request-layer device identity, queue slot, request pointer, and generation before the modern device can complete it. Completion from the modern backend MUST pass through the request-layer completion entry and MUST be rejected or diagnosed if the identity no longer matches.

#### Scenario: matching completion completes pending modern request
- **WHEN** the modern backend reports completion for an accepted pending request with matching device identity, queue slot, request pointer, and generation
- **THEN** the request layer MUST apply the final terminal status to that request and wake eligible waiters
- **AND** the synchronous wrapper MUST return the same final status to its caller

#### Scenario: stale modern completion is rejected
- **WHEN** a modern-backend completion arrives after timeout, cancellation, prior completion, or queue-slot reuse
- **THEN** the request layer MUST reject or diagnose the stale completion
- **AND** it MUST NOT modify the newer request that reused the slot or report a second success for the old request

### Requirement: 现代后端请求上下文边界
Block I/O requests targeting the modern backend SHALL follow the existing request-layer context boundary: submission and synchronous waiting are allowed only from ordinary blockable kernel context, while IRQ-safe completion may only publish terminal state and wake waiters.

#### Scenario: 不可阻塞上下文拒绝现代后端提交
- **WHEN** an IRQ handler, timer tick, scheduler critical section, preemption-disabled region, or equivalent nonblocking path attempts to submit or wait for a modern-backend block request
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT block, allocate unbounded memory, or issue filesystem/cache writeback from that context

#### Scenario: 现代 completion 不执行上层策略
- **WHEN** a modern-backend IRQ or IRQ-like completion source completes a pending request
- **THEN** the request-layer completion entry MUST only publish terminal status and wake waiters
- **AND** it MUST NOT perform cache eviction, dirty scanning, filesystem synchronization, user-memory access, or request submission
