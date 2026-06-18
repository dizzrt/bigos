## ADDED Requirements

### Requirement: 元数据提交计划驱动缓存有序回写

BigOS SHALL drive persistent `/rw` metadata synchronization through an explicit bounded commit plan that selects the dirty cache blocks required by the filesystem mutation. The commit plan MUST preserve the ordered-write constraints for data initialization, inode metadata, directory entries, block mappings, free-space metadata, and volume metadata. The cache MUST write selected blocks in the order required by the plan, propagate deterministic write-back errors, and keep failed blocks dirty or pending.

#### Scenario: 创建文件按计划同步发布块
- **WHEN** a persistent `/rw` file creation reaches synchronization and the commit plan includes initialized child metadata, parent directory data, inode bitmap, and required inode blocks
- **THEN** BigOS MUST synchronize the selected blocks through the cache write-back path in an order that makes the clean-reboot result explainable
- **AND** it MUST NOT report durable creation success until every required block in the plan has synchronized successfully

#### Scenario: 增长文件先同步数据和映射
- **WHEN** a persistent `/rw` file grows and publishes new data block mappings or a larger inode size
- **THEN** BigOS MUST synchronize the initialized data blocks and required block mappings before reporting the enlarged file state as durable
- **AND** after a clean reboot, reads MUST observe synchronized content or zero-filled ranges according to the existing bounded file-growth contract

#### Scenario: 截断释放按顺序同步
- **WHEN** a persistent `/rw` truncate removes inode references and releases backing blocks
- **THEN** BigOS MUST synchronize the metadata that removes the old references before recording the released blocks as durably reusable
- **AND** it MUST NOT create durable block ownership aliases

### Requirement: 提交计划失败保持 pending

BigOS SHALL keep metadata commit state explainable when cache write-back fails. If any selected data or metadata block in a persistent `/rw` commit plan fails to write back, BigOS MUST return a deterministic synchronization error, MUST keep the affected block dirty or pending, and MUST NOT reset the commit plan as durable success. Later synchronization MAY retry the pending state from a blockable process context.

#### Scenario: 提交计划中途写失败
- **WHEN** a metadata commit plan synchronizes several selected blocks and one selected block write fails
- **THEN** BigOS MUST return a deterministic write-back error to the synchronization caller
- **AND** it MUST preserve dirty or pending state for the failed block and any still-required unsynchronized state

#### Scenario: 失败后 fsync 不扩大承诺
- **WHEN** `fsync` or explicit synchronization returns an error because a metadata commit plan failed
- **THEN** BigOS MUST NOT claim that the attempted metadata mutation survives clean reboot
- **AND** previously synchronized persistent state MUST remain explainable within the non-journaled clean-sync boundary

### Requirement: 淘汰不得绕过元数据顺序

BigOS SHALL prevent cache eviction from bypassing persistent `/rw` metadata ordering constraints. If cache pressure selects a dirty metadata block that belongs to an active or pending commit plan, eviction MUST either synchronize that block according to the required ordering constraints or fail deterministically without reusing the cache slot.

#### Scenario: active commit block 淘汰遵守顺序
- **WHEN** cache pressure selects a dirty metadata block that is part of an active persistent `/rw` commit plan
- **THEN** BigOS MUST synchronize it only in a way that preserves the commit plan's ordering constraints
- **AND** it MUST NOT reuse the slot as though unordered metadata write-back had completed successfully

#### Scenario: 无法满足顺序时确定性失败
- **WHEN** cache eviction cannot satisfy the ordering constraints for a selected dirty metadata block
- **THEN** BigOS MUST fail the eviction deterministically and keep the block dirty or pending
- **AND** it MUST NOT publish durable metadata success for the affected mutation
