## ADDED Requirements

### Requirement: 持久后端复用目录树 clean-sync 语义
BigOS SHALL make the persistent clean-sync `/rw` backend reuse the bounded writable directory tree semantics for nested directory creation, empty directory removal, multiple regular-file creation/removal within directories, metadata visibility, bounded enumeration, and deterministic failure behavior. The persistence guarantee MUST remain limited to successful `fsync`, explicit sync, cache write-back, or validation clean shutdown boundaries. This capability MUST NOT claim journaling, crash recovery, power-loss safety, automatic migration, broad storage/device support, or persistence for unsynchronized dirty state.

#### Scenario: 同步后的目录树跨 clean reboot 可见
- **WHEN** a process creates a nested directory, creates multiple files inside it, removes selected entries, synchronizes the persistent `/rw` state successfully, and validation starts a second emulator run with the same persistent test disk
- **THEN** BigOS MUST remount the persistent `/rw` volume
- **AND** the synchronized directory tree state MUST be visible through lookup, metadata queries, bounded enumeration, and fd I/O

#### Scenario: 未同步目录树变更不扩大承诺
- **WHEN** a directory tree mutation succeeds in cache but is not successfully synchronized before reboot
- **THEN** BigOS MUST NOT claim that the mutation persists across reboot
- **AND** previously synchronized filesystem state MUST remain explainable within the non-journaled clean-sync boundary

#### Scenario: 双阶段 marker 验证目录树持久性
- **WHEN** persistent writable smoke validation runs with the existing two-stage marker flow and the required persistent test disk is available
- **THEN** the first stage MUST create and mutate a bounded directory tree, synchronize it, and emit the existing write-stage success or failure marker family
- **AND** the second stage MUST reboot with the same persistent test disk, verify the synchronized directory tree through lookup, metadata, enumeration, and fd I/O, and emit the existing verify-stage success or failure marker family
- **AND** validation MUST NOT use this flow to claim crash recovery or persistence for unsynchronized dirty state

### Requirement: 持久目录树失败不破坏已同步状态
BigOS SHALL keep persistent directory tree failure behavior deterministic and state-preserving. Failed nested create, empty directory removal, unlink, enumeration, metadata update, sync, or cache write-back MUST NOT publish partially initialized files, directories, removed parent entries, leaked inode metadata, dirty-cache success, or corrupted read-only exFAT state.

#### Scenario: 持久目录创建失败不发布半成品
- **WHEN** persistent `/rw` directory creation requires a directory slot, inode, data block, cache block, or kernel allocation that is unavailable
- **THEN** BigOS MUST return a deterministic capacity, memory, or I/O error
- **AND** after the failure, later lookup, metadata query, and directory enumeration MUST observe the pre-failure persistent state

#### Scenario: 同步失败不报告目录树持久成功
- **WHEN** `fsync`, explicit sync, or cache eviction fails while writing dirty directory tree metadata to the persistent block device
- **THEN** BigOS MUST report a deterministic write-back error
- **AND** it MUST NOT mark the affected metadata clean or report the attempted directory tree update as durably committed
