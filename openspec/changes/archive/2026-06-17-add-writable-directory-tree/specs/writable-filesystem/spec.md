## ADDED Requirements

### Requirement: 可写后端承载目录树
BigOS SHALL extend the existing writable `/rw` backend so that its create, open, unlink, metadata, directory enumeration, and restricted rename behavior operates over a bounded directory tree rather than only isolated root-level objects. The backend MUST enforce parent-directory permissions, component type checks, component length limits, capacity limits, and deterministic rollback for failed directory tree mutations. This extension MUST NOT add full POSIX filesystem semantics, broad file-backed `mmap`, journaling, crash recovery, hard links, symbolic links, complete directory rename, or cross-mount rename.

#### Scenario: 父目录内多文件创建删除
- **WHEN** a process creates several regular files inside an existing writable `/rw` directory, opens them independently, and later unlinks selected files
- **THEN** each successful file creation MUST be independently visible through lookup and fd I/O
- **AND** each successful unlink MUST remove only the selected directory entry while leaving sibling entries and open fd state explainable

#### Scenario: 父目录权限拒绝创建
- **WHEN** a process attempts to create a file or directory in a parent directory that does not grant mutation permission to the caller
- **THEN** BigOS MUST return a deterministic access-denied error
- **AND** it MUST NOT allocate or publish a new inode, directory entry, or fd

#### Scenario: 失败回滚不污染目录
- **WHEN** file or directory creation under `/rw` fails after some resources were tentatively reserved
- **THEN** BigOS MUST release or hide the tentative resources before returning
- **AND** parent directory enumeration, metadata, and later creation attempts MUST remain explainable from the pre-failure state

### Requirement: 空目录删除纳入可写后端
BigOS SHALL add a writable-backend operation for removing empty directories under `/rw`. The operation MUST reject missing paths, regular files, non-empty directories, read-only backend targets, and unsupported object types with deterministic negative errno values through VFS/syscall translation. It MUST NOT provide recursive removal.

#### Scenario: 空目录删除释放目录项
- **WHEN** an empty `/rw` directory is removed successfully
- **THEN** its parent directory MUST no longer contain the removed entry
- **AND** later creation MAY reuse the freed directory slot or inode within the backend's bounded allocation rules

#### Scenario: 非空目录删除保持原状态
- **WHEN** directory removal is attempted on a directory containing one or more live entries
- **THEN** BigOS MUST fail with a deterministic not-empty error
- **AND** all child entries, parent entries, inode metadata, and data blocks MUST remain unchanged
