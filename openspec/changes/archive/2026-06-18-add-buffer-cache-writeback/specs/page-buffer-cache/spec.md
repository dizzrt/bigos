## ADDED Requirements

### Requirement: 设备范围 dirty block 同步

BigOS SHALL provide a bounded `sync_device()` or equivalent device-scoped page/buffer cache write-back path for dirty blocks belonging to a selected block device. The write-back path MUST write each selected dirty block through the existing synchronous block-device write path, MUST clear dirty state only after the underlying write succeeds, MUST return a deterministic error for the first failed write, and MUST keep failed blocks dirty or pending. Existing global `sync_all()` MAY remain available as a debug or internal maintenance helper, but persistent `/rw` clean-sync success MUST be based on device-scoped or selected-block synchronization rather than an accidental global flush. This capability MUST remain process-context-only and MUST NOT introduce async I/O, request queues, new storage drivers, or SMP cache coherency.

#### Scenario: 设备同步成功清除 dirty
- **WHEN** a writable backend requests synchronization for dirty cache blocks belonging to its backing block device from a blockable process context
- **THEN** BigOS MUST write the selected dirty blocks to that block device
- **AND** it MUST clear each block's dirty state only after the corresponding device write succeeds

#### Scenario: 设备同步失败保留 dirty
- **WHEN** device-scoped synchronization encounters a backing block-device write error or timeout
- **THEN** BigOS MUST return a deterministic write-back error to the caller
- **AND** it MUST keep the failed block dirty or otherwise represented as pending write-back state

#### Scenario: 设备同步不扩大无关设备承诺
- **WHEN** dirty cache blocks for multiple devices exist and the caller synchronizes one selected device
- **THEN** BigOS MUST NOT report durable success for dirty blocks belonging to other devices
- **AND** later documentation or validation MUST describe only the selected device's synchronized state as clean-sync eligible

#### Scenario: sync_all 保留为内部工具
- **WHEN** an internal diagnostic or maintenance path intentionally invokes global cache synchronization
- **THEN** BigOS MAY use the existing global helper for that internal purpose
- **AND** persistent writable filesystem success paths MUST still use device-scoped or selected-block synchronization to define their durable contract

### Requirement: dirty victim 淘汰写回不丢数据

BigOS SHALL make cache eviction of dirty unreferenced victims use the same write-back failure semantics as explicit synchronization. A dirty victim MAY be reused only after its write-back succeeds. If write-back fails, the cache MUST keep the victim associated with its original device/block key, MUST keep its dirty or pending state, and MUST return deterministic failure to the caller instead of silently reusing the slot.

#### Scenario: dirty victim 写回成功后复用
- **WHEN** the cache is full, an unreferenced dirty block is selected as the only reusable victim, and write-back succeeds
- **THEN** BigOS MAY reuse the cache slot for the newly requested device/block key
- **AND** a later reload of the old block from the backing device MUST observe the written content

#### Scenario: dirty victim 写回失败不复用
- **WHEN** the cache is full, an unreferenced dirty block is selected as a victim, and write-back fails
- **THEN** BigOS MUST keep the victim dirty and associated with its original device/block key
- **AND** it MUST fail the new cache request deterministically rather than returning a slot containing stale or uncommitted data

### Requirement: 回写上下文边界可诊断

BigOS SHALL keep cache load, synchronization, and eviction write-back outside IRQ context, scheduler critical sections, preemption-disabled regions, and other nonblocking contexts. If a cache write-back path is reached from a nonblocking context, BigOS MUST fail deterministically or enter a documented diagnostic path, and MUST NOT issue synchronous block-device I/O from that context.

#### Scenario: 可阻塞上下文允许同步
- **WHEN** cache synchronization or dirty eviction runs from an ordinary blockable process context
- **THEN** BigOS MAY perform bounded allocation already allowed by cache initialization and synchronous block-device I/O

#### Scenario: 不可阻塞上下文拒绝同步
- **WHEN** cache synchronization or dirty eviction is attempted from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT publish successful write-back or issue blocking storage I/O from that context
