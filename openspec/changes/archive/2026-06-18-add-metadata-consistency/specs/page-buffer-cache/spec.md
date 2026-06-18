## ADDED Requirements

### Requirement: 缓存支持元数据 ordered commit
BigOS SHALL allow filesystem code to synchronize persistent `/rw` metadata dirty blocks according to an explicit ordered commit plan. The cache MUST support flushing selected dirty metadata blocks, preserving block identity by device and block number, propagating write-back errors, and keeping failed blocks dirty or pending. This requirement MUST retain the existing process-context-only boundary and MUST NOT perform blocking persistent I/O from IRQ context, scheduler critical sections, preemption-disabled regions, or other nonblocking paths.

#### Scenario: 按提交计划同步 dirty metadata blocks
- **WHEN** persistent `/rw` submits an ordered metadata commit plan to the cache from a blocking process context
- **THEN** the cache MUST write the selected dirty blocks in the requested order or return a deterministic error at the failing block
- **AND** it MUST NOT report success before all required blocks in the plan are synchronized

#### Scenario: 不可阻塞上下文拒绝 metadata writeback
- **WHEN** metadata commit write-back is attempted from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT issue blocking device I/O or publish durable metadata success

### Requirement: metadata 写回失败保留 dirty 或 pending 状态
BigOS SHALL keep cache state explainable when persistent metadata write-back fails. If a backing block write for a metadata block returns an error or timeout, the cache MUST return a deterministic error, MUST NOT mark the affected block clean, and MUST NOT let `fsync`, explicit sync, or cache eviction report durable success for the affected metadata commit.

#### Scenario: dirty metadata block 写失败
- **WHEN** the cache attempts to write a dirty metadata block for persistent `/rw` and the backing block device reports failure
- **THEN** the cache MUST keep that block dirty or pending
- **AND** it MUST return a deterministic write-back error to the caller

#### Scenario: eviction 不绕过 ordered commit
- **WHEN** cache pressure selects a dirty persistent metadata block that belongs to an ordered commit plan
- **THEN** eviction MUST either synchronize it according to the required ordering constraints or fail deterministically
- **AND** it MUST NOT reuse the cache slot as though unordered metadata write-back had completed successfully
