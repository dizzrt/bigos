## ADDED Requirements

### Requirement: 缓存可使用现代块存储后端
BigOS SHALL allow the page/buffer cache to use a published modern block-storage backend as a target device through the block I/O request layer. Cache load, dirty update, explicit synchronization, device-scoped synchronization, and eviction writeback MUST preserve the existing dirty-state and deterministic error semantics.

#### Scenario: cache 经现代后端读写往返
- **WHEN** validation obtains a cache block for the modern block-storage backend, modifies it, marks it dirty, synchronizes it, and later reads the same device/block key again
- **THEN** the cache path MUST submit backend I/O through the block request layer
- **AND** the later read MUST observe the latest contents that were successfully synchronized to the modern backend

#### Scenario: 现代后端读装入失败返回错误
- **WHEN** cache load for a modern-backend device/block key encounters request validation failure, queue exhaustion, issue failure, timeout, completion rejection, or device error
- **THEN** the cache MUST return a deterministic error to the caller
- **AND** it MUST NOT publish the target cache block as valid data for that key

### Requirement: 现代后端写回成功后才清 dirty
BigOS SHALL clear dirty state for cache blocks backed by the modern storage backend only after the request-layer synchronous wrapper observes terminal write success. Pending, timeout, queue-full, issue failure, completion rejection, and device error MUST be treated as writeback failure.

#### Scenario: 现代后端写回成功清 dirty
- **WHEN** a dirty cache block belonging to the modern backend is explicitly synchronized or selected for writeback from a blockable kernel context and the request layer reports terminal write success
- **THEN** the cache MAY clear the dirty state for that block
- **AND** a later reload from the same modern backend device/block key MUST observe the written content

#### Scenario: 现代后端写回失败保留 dirty
- **WHEN** writeback of a dirty cache block to the modern backend fails because of request-layer rejection, queue capacity, issue failure, timeout, completion rejection, or device error
- **THEN** the cache MUST return a deterministic writeback error
- **AND** it MUST keep the block dirty or otherwise preserve an explainable pending-write state

### Requirement: 现代后端 dirty victim 淘汰不丢数据
BigOS SHALL make eviction of dirty unreferenced cache blocks backed by the modern backend use the same writeback success boundary as explicit synchronization. A dirty victim MAY be reused only after the modern-backend write request reaches terminal success.

#### Scenario: dirty victim 写回成功后复用
- **WHEN** the cache is full, an unreferenced dirty block backed by the modern backend is selected as the only reusable victim, and writeback succeeds through the request layer
- **THEN** BigOS MAY reuse the cache slot for a new device/block key
- **AND** a later reload of the old key from the modern backend MUST observe the written content

#### Scenario: dirty victim 写回失败不复用
- **WHEN** dirty victim writeback to the modern backend fails or times out
- **THEN** BigOS MUST keep the victim associated with its original modern backend device/block key and dirty or pending state
- **AND** it MUST return deterministic failure instead of silently reusing the slot

### Requirement: 现代后端缓存同步保持上下文边界
Cache load, explicit writeback, device-scoped synchronization, and eviction involving the modern backend SHALL run only from ordinary blockable kernel context. Modern-backend IRQ or completion context MUST NOT execute cache policy.

#### Scenario: 可阻塞上下文同步现代后端缓存块
- **WHEN** cache synchronization for modern-backend blocks is invoked from an allowed blockable kernel context
- **THEN** BigOS MAY submit bounded synchronous wrapper requests through the block I/O request layer
- **AND** it MUST publish success only after terminal success for the selected blocks

#### Scenario: completion 不触发现代后端 cache policy
- **WHEN** a modern-backend block request completes from IRQ context or an IRQ-like producer
- **THEN** BigOS MUST only complete the pending request and wake the waiter through the request layer
- **AND** it MUST NOT run cache eviction, dirty scanning, device-scoped sync, or filesystem writeback from that completion path
