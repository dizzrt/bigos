## ADDED Requirements

### Requirement: 既有块路径通过非轮询完成闭环
BigOS SHALL route existing kernel block I/O submissions through a bounded nonpolling issue-to-completion path. A synchronous caller MAY continue to use the existing blocking wrapper, but the wrapper MUST arm request completion, issue device work, wait through the scheduler boundary, and return only the final request-layer status. The wrapper MUST NOT depend on the block device implementation spinning in the submission call stack until the whole request completes.

#### Scenario: 同步 wrapper 等待 completion
- **WHEN** a valid cache or filesystem block request is submitted through the synchronous block I/O API from blockable kernel context
- **THEN** BigOS MUST arm bounded completion state before issuing the device operation
- **AND** the caller MUST receive success, timeout, or a deterministic failure only after the request completion path reaches a terminal state

#### Scenario: 设备路径不以同步轮询完成请求
- **WHEN** the existing ATA-backed block path or another migrated backend accepts a request from the request layer
- **THEN** the backend MUST issue bounded device work and complete the request through the request-layer completion boundary
- **AND** it MUST NOT hide whole-request completion behind a synchronous polling loop in the submitted read/write call

#### Scenario: 非轮询路径保持有界等待
- **WHEN** the device does not complete before the configured bounded wait expires
- **THEN** BigOS MUST return a deterministic timeout status to the synchronous wrapper
- **AND** a later completion for the same request identity MUST be rejected or diagnosed without completing a reused queue slot

### Requirement: 块设备 issue 边界可表达同步兼容与中断完成
BigOS SHALL provide a block device submission boundary that can express whether a backend completes immediately without polling, enters pending interrupt-driven completion, or fails before issue. The boundary MUST preserve operation type, device identity, LBA, sector count, kernel buffer, status mapping, and bounded ownership without requiring hosted runtime services, heap allocation from IRQ context, exceptions, RTTI, DMA, or a new storage ISA.

#### Scenario: issue 成功进入 pending
- **WHEN** a valid request is issued to a backend that supports interrupt-driven completion
- **THEN** BigOS MUST keep the request associated with its queue slot and completion token
- **AND** the backend completion source MUST be able to complete only that accepted request

#### Scenario: issue 前失败不进入 pending
- **WHEN** device issue fails because the device is not ready, the operation is unsupported, the request is invalid, or the queue cannot accept the request
- **THEN** BigOS MUST return the corresponding deterministic request-layer status
- **AND** it MUST NOT leave a request reported as pending or queued

#### Scenario: 同步兼容后端不扩大承诺
- **WHEN** a backend can complete without hardware waiting, such as a RAM validation backend
- **THEN** BigOS MAY complete the request immediately through the same request-layer status path
- **AND** it MUST NOT describe this as user-visible async I/O, DMA, broad storage scheduling, or background writeback

### Requirement: ATA PIO 迁移保持端口访问和中断边界可诊断
BigOS SHALL migrate the existing ATA PIO block path away from whole-request synchronous polling while preserving explicit port I/O ordering, LBA range checks, 512-byte sector semantics, deterministic device status mapping, and current boot/storage layout assumptions. ATA IRQ or an equivalent bounded completion source MUST complete pending requests through the request-layer completion entry and MUST leave irqchip EOI ownership with the interrupt dispatch owner.

#### Scenario: ATA 读请求完成
- **WHEN** an ATA-backed read request is issued and the device reports the data phase or final completion through the migrated completion source
- **THEN** BigOS MUST transfer the requested sector data into the caller-provided kernel buffer within bounded request limits
- **AND** it MUST complete the request with success only after the requested data has been made visible to the waiting submitter

#### Scenario: ATA 写请求和 flush 完成
- **WHEN** an ATA-backed write request is issued and the device reports write data acceptance and cache flush completion or a deterministic error
- **THEN** BigOS MUST complete the request with success only after the write/flush contract used by the current path is satisfied
- **AND** device timeout, fault, or error MUST propagate as deterministic request-layer failure

#### Scenario: ATA IRQ 不拥有块层策略
- **WHEN** the ATA IRQ or equivalent completion source runs
- **THEN** it MUST perform only bounded device-state observation, required PIO transfer for the accepted request, request completion, and scheduler wakeup handoff
- **AND** it MUST NOT perform filesystem work, cache writeback policy, user-memory access, blocking waits, dynamic allocation, or irqchip EOI inside the request completion entry

### Requirement: 非轮询迁移不改变 cache/writeback 外部语义
BigOS SHALL preserve existing page/buffer cache and writable filesystem behavior when block requests are completed through the nonpolling path. Cache read miss, write-back dirty state, eviction writeback, `sync`/`fsync`, persistent `/rw` clean-sync, and deterministic error propagation MUST behave as before from callers' perspective.

#### Scenario: cache read miss 仍返回已装入数据
- **WHEN** a cache read miss submits a block read through the migrated request path and completion succeeds
- **THEN** the cache MUST mark the block valid and return the loaded data to the caller
- **AND** it MUST NOT expose partial or stale data while the request is pending

#### Scenario: dirty writeback 失败保留 dirty
- **WHEN** cache writeback or eviction submits a write through the migrated request path and completion returns timeout or device error
- **THEN** BigOS MUST keep the affected block dirty or otherwise represented as pending writeback state
- **AND** it MUST NOT report durable synchronization success

#### Scenario: persistent clean-sync 行为不变
- **WHEN** persistent `/rw` performs a clean-sync operation and all required data and metadata block requests complete successfully
- **THEN** BigOS MUST report success according to the existing clean-sync contract
- **AND** a later clean reboot validation MUST observe the synchronized contents when emulator and disk setup are available

### Requirement: 非轮询块路径验证可复现
BigOS SHALL provide deterministic validation for the migrated block path. Validation MUST cover successful read/write completion, cache round-trip, dirty writeback failure retention, request timeout, late completion rejection, default boot regression, and skipped runtime coverage when emulator or toolchain prerequisites are unavailable.

#### Scenario: 默认关闭 smoke 覆盖迁移闭环
- **WHEN** the block I/O request smoke or a dedicated nonpolling block smoke is enabled with the expected toolchain and emulator disk setup
- **THEN** validation MUST exercise at least one request that is issued, completed through the completion boundary, and observed by a synchronous waiting caller
- **AND** it MUST emit deterministic pass/fail diagnostics according to the existing smoke style

#### Scenario: cache 和 writeback 被覆盖
- **WHEN** migrated block-path validation runs
- **THEN** it MUST cover cache read/write round-trip and a deterministic writeback failure path
- **AND** it MUST verify that failure does not clear dirty state or claim durable success

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display dependencies, serial logging, or disk image setup required by runtime validation are unavailable
- **THEN** validation notes MUST record the skipped nonpolling block coverage and residual risk
- **AND** they MUST NOT claim runtime smoke success for the skipped environment
