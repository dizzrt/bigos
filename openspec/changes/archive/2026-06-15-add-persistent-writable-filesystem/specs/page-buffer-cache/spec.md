## ADDED Requirements

### Requirement: 持久后端缓存同步跨重启可验证
BigOS SHALL make page/buffer cache write-back for a persistent writable backend durable enough for clean-reboot validation. A successful `fsync`, explicit sync, or eviction write-back on persistent `/rw` MUST write all required dirty data and metadata blocks to the underlying writable block device before reporting success. The cache MUST retain the existing process-context-only boundary for load and write-back and MUST NOT perform blocking persistent I/O from IRQ context, scheduler critical sections, preemption-disabled regions, or other nonblocking paths.

#### Scenario: fsync 成功后 clean reboot 读回
- **WHEN** a persistent `/rw` file has dirty data and metadata blocks and the caller invokes `fsync`
- **THEN** the cache MUST write the required blocks to the persistent block device before `fsync` reports success
- **AND** after a clean reboot and remount, reading the file MUST return the synchronized content

#### Scenario: 淘汰脏块成功后持久介质可读
- **WHEN** cache pressure evicts an unreferenced dirty block belonging to persistent `/rw` and the write-back succeeds
- **THEN** the cache MAY reuse the slot
- **AND** later reload from the persistent block device MUST observe the written block content

#### Scenario: 不可阻塞上下文拒绝持久写回
- **WHEN** persistent cache load or write-back is attempted from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT issue blocking device I/O or publish a successful persistent commit from that context

### Requirement: 持久写回失败保留脏状态
BigOS SHALL keep dirty cache state explainable when persistent write-back fails. If a persistent block device write returns an error or timeout, the cache MUST return a deterministic error, MUST NOT mark the affected block clean, and MUST NOT report `fsync` or sync success for the affected filesystem state.

#### Scenario: 块设备写失败不清除 dirty
- **WHEN** persistent cache write-back fails because the underlying writable block device reports an error or timeout
- **THEN** the cache MUST keep the affected block dirty or otherwise preserve an explainable pending-write state
- **AND** it MUST return a deterministic error to the caller

#### Scenario: 同步失败不扩大持久性承诺
- **WHEN** `fsync` or explicit sync returns an error for persistent `/rw`
- **THEN** BigOS MUST NOT claim the attempted update survives reboot
- **AND** previously synchronized filesystem state MUST remain explainable according to the non-journaled persistent filesystem boundary
