## ADDED Requirements

### Requirement: cache supports journal ordered flush plans
BigOS SHALL allow filesystem code to submit a bounded ordered flush plan that covers journal blocks and home-location blocks for the same backing device. The cache MUST flush blocks in the requested phase order, MUST propagate the first deterministic write-back error, and MUST keep failed blocks dirty or pending. This capability MUST remain process-context-only and MUST NOT perform blocking I/O from IRQ context, scheduler critical sections, preemption-disabled regions, or other nonblocking paths.

#### Scenario: journal phases flush in order
- **WHEN** persistent `/rw` submits an ordered journal commit plan containing journal descriptor/payload blocks, a journal commit marker block, home-location blocks, and a journal checkpoint/clear block
- **THEN** the cache MUST write the selected dirty blocks in the requested order
- **AND** it MUST NOT report success before every required block in every phase has reached terminal write success

#### Scenario: ordered flush fails deterministically
- **WHEN** a selected block in a journal ordered flush plan fails because of request-layer error, queue capacity, backing-device error, timeout, or nonblocking-context rejection
- **THEN** the cache MUST return a deterministic error for the plan
- **AND** the failed block MUST remain dirty or otherwise represented as pending write-back state

### Requirement: cache eviction does not bypass journal ordering
BigOS SHALL prevent dirty cache eviction from publishing persistent `/rw` home-location blocks in an order that violates an active journal transaction. A dirty block protected by a journal transaction MUST either be flushed through the journal ordered plan or cause deterministic eviction failure until the transaction completes.

#### Scenario: protected home block selected as eviction victim
- **WHEN** cache pressure selects a dirty persistent `/rw` home-location block that belongs to an active or pending journal transaction
- **THEN** eviction MUST NOT write that block directly as an unordered dirty victim
- **AND** eviction MUST either use the required journal ordering or fail deterministically without reusing the cache slot

#### Scenario: unrelated device dirty block is unaffected
- **WHEN** journal ordering protects dirty blocks for one persistent `/rw` backing device and dirty blocks for another device are present
- **THEN** the ordered journal plan MUST NOT claim durable success for unrelated device blocks
- **AND** unrelated device dirty blocks MUST retain their existing device-scoped synchronization semantics

### Requirement: selected journal writes preserve block identity
BigOS SHALL preserve `(device, block_no)` identity for journal and home-location selected writes. The cache MUST NOT coalesce, remap, or globally flush journal-protected blocks in a way that changes the filesystem's ordered commit plan or expands the durable success scope.

#### Scenario: journal block identity is stable
- **WHEN** persistent `/rw` records a journal payload block and asks the cache to synchronize that block
- **THEN** the cache MUST write the cached block associated with the exact backing device and block number requested
- **AND** a missing or clean selected block MUST be handled according to the documented selected-write semantics without marking unrelated blocks durable
