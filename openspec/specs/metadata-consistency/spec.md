# metadata-consistency Specification

## Purpose
TBD - created by archiving change add-metadata-consistency. Update Purpose after archive.
## Requirements
### Requirement: 有界元数据提交单元
BigOS SHALL define bounded metadata commit units for persistent `/rw` filesystem mutations. A commit unit MUST identify the affected directory entries, inode metadata, file size, block mappings, free-space metadata, and volume metadata needed to make one filesystem mutation durable. Commit units MUST remain bounded by existing inode, directory-entry, block, cache, and kernel allocation limits. This capability MUST NOT imply complete POSIX filesystem semantics, journal replay, crash recovery, power-loss recovery, async I/O, or broad storage/device support.

#### Scenario: 创建文件的提交单元覆盖必要元数据
- **WHEN** a process creates a regular file under persistent `/rw` and the operation reaches synchronization
- **THEN** BigOS MUST include the new inode metadata, parent directory entry, free-space metadata, and any required volume metadata in the durable commit unit
- **AND** it MUST NOT report durable success until the required metadata blocks are synchronized successfully

#### Scenario: 提交单元容量不足失败
- **WHEN** constructing a metadata commit unit requires more bounded metadata records, cache blocks, or kernel allocations than available
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** later lookup, metadata query, directory enumeration, and file reads MUST observe the pre-commit state

### Requirement: ordered write 最小一致性策略
BigOS SHALL use an ordered-write strategy for persistent `/rw` metadata updates. Data or newly initialized blocks required by a visible metadata update MUST be synchronized before the metadata that publishes references to those blocks. Metadata that removes references MUST be synchronized before released blocks are made durable as reusable. The strategy MUST keep clean-reboot results explainable without claiming recovery from an interrupted write sequence.

#### Scenario: 发布目录项前同步被引用对象
- **WHEN** a persistent `/rw` mutation creates a directory entry that references a newly initialized inode or block mapping
- **THEN** BigOS MUST synchronize the referenced inode and required initialized blocks before the directory entry is durably published

#### Scenario: 释放块前移除旧引用
- **WHEN** a persistent `/rw` mutation truncates or removes an object and releases data blocks
- **THEN** BigOS MUST durably remove the old inode references before those blocks are durably recorded as reusable
- **AND** it MUST NOT create durable block ownership aliases

### Requirement: 同步失败保持 pending 状态
BigOS SHALL keep persistent metadata state explainable when synchronization fails. If block I/O, cache write-back, cache capacity, kernel allocation, or consistency checks fail during metadata synchronization, BigOS MUST return a deterministic error and MUST NOT mark the affected metadata commit as durably complete. Dirty metadata MUST remain dirty or otherwise represented as pending until a later successful synchronization or teardown.

#### Scenario: metadata block 写回失败
- **WHEN** synchronization of a dirty metadata block for persistent `/rw` fails because the backing block device reports an error
- **THEN** BigOS MUST return a deterministic write-back error to the synchronization caller
- **AND** it MUST keep the affected metadata dirty or pending rather than clearing it as durable

#### Scenario: fsync 失败不扩大持久性承诺
- **WHEN** `fsync` or explicit sync returns an error while synchronizing persistent `/rw` metadata
- **THEN** BigOS MUST NOT claim that the attempted metadata update survives clean reboot
- **AND** previously synchronized filesystem state MUST remain explainable within the bounded clean-sync contract

### Requirement: 重新挂载时校验元数据不变量
BigOS SHALL validate persistent `/rw` metadata invariants before publishing the volume as writable after a clean boot. Mount-time validation MUST check compatible format metadata, root metadata bounds, inode bounds, directory-entry bounds, block mapping bounds, and free-space ownership consistency within the bounded filesystem model. Validation MUST reject incompatible or internally contradictory metadata deterministically and MUST NOT auto-repair, auto-format, auto-migrate, or modify read-only exFAT boot assets.

#### Scenario: 一致卷重新挂载
- **WHEN** the persistent test disk contains compatible metadata whose inode, directory, block mapping, and free-space invariants pass validation
- **THEN** BigOS MUST publish the volume as persistent `/rw`
- **AND** synchronized files and directories MUST be visible through lookup, metadata query, enumeration, and fd I/O

#### Scenario: 块所有权矛盾拒绝挂载
- **WHEN** mount-time validation finds a block recorded as both owned by a live inode mapping and available in the free-space metadata
- **THEN** BigOS MUST reject the persistent writable mount with a deterministic diagnostic or error path
- **AND** it MUST NOT silently repair the block ownership or expose the volume as writable

### Requirement: 元数据一致性验证可复现
BigOS SHALL provide default-off validation for persistent `/rw` metadata consistency under the current x86_64 Legacy BIOS emulator path. Validation MUST cover ordered metadata commits, clean reboot readback, deterministic synchronization failure behavior where practical, mount-time rejection of inconsistent metadata where practical, and explicit recording of unavailable toolchain, emulator, ROM/display, or persistent disk dependencies.

#### Scenario: 双阶段验证同步后的元数据
- **WHEN** metadata consistency validation runs with the required toolchain, emulator, and persistent test disk available
- **THEN** the first run MUST create, mutate, and synchronize a bounded mix of files and directories
- **AND** the second run MUST remount the same disk and verify synchronized directory entries, inode metadata, file sizes, block mappings, and free-space effects

#### Scenario: 环境不可用时记录跳过
- **WHEN** xmake, the x86_64-elf toolchain, QEMU or Bochs, ROM/display support, serial capture, or the required persistent disk image is unavailable
- **THEN** validation MUST record the missing dependency and residual risk
- **AND** it MUST NOT report runtime metadata consistency validation as passed

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
