## ADDED Requirements

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
