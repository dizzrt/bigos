## ADDED Requirements

### Requirement: 持久文件增长元数据有序提交
BigOS SHALL apply ordered metadata commits to persistent `/rw` file growth and truncate operations. Successful synchronization of file growth or truncate MUST durably order data initialization, inode size updates, block mapping updates, and free-space metadata updates so clean reboot readback observes a consistent file size and block ownership state. This requirement MUST NOT imply broad sparse-file support, writable file-backed `mmap`, journal replay, crash recovery, or persistence for unsynchronized dirty state.

#### Scenario: 增长文件同步后 size 和映射一致
- **WHEN** a persistent `/rw` file grows through append, cross-block write, or seek-past-EOF write and synchronization succeeds
- **THEN** BigOS MUST durably synchronize the required data initialization and block mappings before reporting the enlarged size as durable
- **AND** after a clean reboot, reads MUST observe the synchronized contents and zero-filled gap ranges

#### Scenario: 收缩截断同步后释放块可解释
- **WHEN** a persistent `/rw` file is truncated to a smaller size and synchronization succeeds
- **THEN** BigOS MUST durably remove inode references to released blocks before recording those blocks as reusable
- **AND** after a clean reboot, metadata MUST report the smaller size and reads beyond the new EOF MUST follow the existing EOF behavior

### Requirement: 文件增长元数据失败不发布 durable success
BigOS SHALL keep persistent file growth and truncate metadata state-preserving on failure. If capacity checks, cache allocation, kernel allocation, user-buffer validation, metadata synchronization, or backing block I/O fails, BigOS MUST NOT publish a durable partial file size, partial block mapping, duplicated block ownership, stale-data exposure, dirty-cache success, or unintended fd offset advancement.

#### Scenario: size metadata 写回失败
- **WHEN** persistent `/rw` file growth writes data successfully in runtime state but fails while synchronizing inode size metadata
- **THEN** `fsync` or explicit sync MUST return a deterministic error
- **AND** BigOS MUST NOT claim the enlarged size survives clean reboot

#### Scenario: free-space metadata 写回失败
- **WHEN** persistent `/rw` truncate removes inode references but fails while synchronizing free-space metadata
- **THEN** BigOS MUST keep the affected metadata dirty or pending and report synchronization failure
- **AND** it MUST NOT reuse the released blocks as durably free before the metadata commit succeeds
