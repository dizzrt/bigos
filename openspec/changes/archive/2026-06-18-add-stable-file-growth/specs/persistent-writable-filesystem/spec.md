## ADDED Requirements

### Requirement: 持久后端同步文件增长跨 clean reboot 可见
BigOS SHALL make successfully synchronized persistent `/rw` file growth visible after a clean reboot using the same persistent test disk. The clean-sync guarantee MUST include synchronized append writes, seek-past-EOF writes with zero-read gaps, cross-block writes, file size metadata, and the block mappings needed to read the synchronized content. This requirement MUST NOT claim journal replay, crash recovery, power-loss safety, or persistence for unsynchronized dirty state.

#### Scenario: 同步后的扩展文件第二次启动读回
- **WHEN** a user process grows a persistent `/rw` regular file, calls `fsync` or equivalent explicit sync successfully, and validation starts a second emulator run with the same persistent test disk
- **THEN** BigOS MUST remount the persistent `/rw` volume
- **AND** reopening the file MUST return the synchronized content, zero-filled gap ranges, and bounded size metadata

#### Scenario: 未同步增长不扩大持久性承诺
- **WHEN** a persistent `/rw` file grows in cache but the affected data and metadata are not successfully synchronized before reboot
- **THEN** BigOS MUST NOT claim that the growth survives reboot
- **AND** previously synchronized filesystem state MUST remain explainable within the non-journaled clean-sync boundary

### Requirement: 持久后端同步截断跨 clean reboot 可见
BigOS SHALL make successfully synchronized persistent `/rw` truncate results visible after a clean reboot. The guarantee MUST include the truncated file size, retained prefix contents, zero-filled extended ranges, released-block ownership metadata, and absence of user-visible stale data from reused blocks. Failed or unsynchronized truncates MUST NOT be described as durable.

#### Scenario: 同步后的收缩截断第二次启动可见
- **WHEN** a persistent `/rw` file is truncated to a smaller size and the change synchronizes successfully before a clean reboot
- **THEN** the second run MUST observe the smaller size and retained prefix contents
- **AND** reads beyond the new EOF MUST follow the existing EOF behavior

#### Scenario: 同步后的扩展截断第二次启动零读
- **WHEN** a persistent `/rw` file is truncated to a larger size and the change synchronizes successfully before a clean reboot
- **THEN** the second run MUST observe the larger size
- **AND** the newly extended range MUST read as zero bytes until overwritten

### Requirement: 持久块分配失败保持已同步状态
BigOS SHALL keep persistent `/rw` state explainable when file growth, truncate, free-space metadata update, cache write-back, or backing block I/O fails. Failed persistent operations MUST NOT publish partially initialized blocks, leaked free-space metadata, duplicated block ownership, truncated metadata, dirty-cache success, or corrupted read-only exFAT state.

#### Scenario: 持久块耗尽不发布半成品
- **WHEN** persistent `/rw` file growth or truncate requires a data block, inode mapping, cache block, or kernel allocation that is unavailable
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** later lookup, read, metadata query, and directory enumeration MUST observe the pre-failure persistent state

#### Scenario: 持久写回失败不清除 pending state
- **WHEN** `fsync`, explicit sync, or cache eviction fails while writing persistent file growth or truncate metadata
- **THEN** BigOS MUST report a deterministic write-back error
- **AND** it MUST NOT mark the affected filesystem state as durably committed
