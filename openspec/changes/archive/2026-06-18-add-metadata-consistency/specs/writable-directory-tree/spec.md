## ADDED Requirements

### Requirement: 持久目录树 mutation 使用有序元数据提交
BigOS SHALL apply ordered metadata commits to persistent `/rw` directory tree mutations. Successful create, unlink, rmdir, and bounded regular-file rename operations MUST synchronize the affected parent directory entries, child inode metadata, directory metadata, and free-space metadata in an order that keeps clean-reboot state explainable. This requirement MUST NOT introduce complete POSIX directory rename, atomic replacement, hard links, symbolic links, mount namespaces, journal replay, or crash recovery.

#### Scenario: 创建目录同步后可见
- **WHEN** a process creates a directory under persistent `/rw` and synchronization succeeds
- **THEN** the child directory inode metadata MUST be durable before the parent directory entry is durably published
- **AND** after a clean reboot, lookup and bounded enumeration MUST observe the created directory

#### Scenario: 删除目录同步后不残留目录项
- **WHEN** a process removes an empty directory under persistent `/rw` and synchronization succeeds
- **THEN** the parent directory entry removal and released metadata ownership MUST be durably ordered
- **AND** after a clean reboot, lookup and bounded enumeration MUST not report the removed directory

### Requirement: 持久目录树失败不发布半成品元数据
BigOS SHALL keep persistent directory tree metadata state-preserving on failure. Failed create, unlink, rmdir, rename, directory metadata update, cache write-back, or backing block I/O MUST NOT publish partially initialized directory entries, orphan live inodes, leaked free-space metadata, duplicated block ownership, dirty-cache success, or corrupted read-only exFAT state.

#### Scenario: 创建文件目录项前同步失败
- **WHEN** persistent `/rw` file creation initializes an inode but metadata synchronization fails before the parent directory entry is durably committed
- **THEN** BigOS MUST return a deterministic synchronization error
- **AND** after clean reboot, the new name MUST NOT appear as a partially created directory entry

#### Scenario: rename 中途失败保持源目标可解释
- **WHEN** a bounded persistent `/rw` regular-file rename fails while synchronizing source or destination parent metadata
- **THEN** BigOS MUST return a deterministic error
- **AND** later lookup, metadata query, and directory enumeration MUST observe either the pre-operation state or a documented pending state that is not reported as durable success
