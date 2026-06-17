## ADDED Requirements

### Requirement: 有界可写目录树
BigOS SHALL provide a bounded writable directory tree under `/rw` for the active writable backend. The directory tree MUST support nested directory creation, empty directory removal, creation and removal of multiple regular files within directories, path lookup through existing directories, and metadata visibility for created objects. The behavior MUST remain bounded by path length, component name length, inode count, directory entry count, file size, open file count, and available kernel/cache resources. This capability MUST NOT imply hard links, symbolic links, mount namespaces, complete POSIX directory semantics, complete directory rename, cross-mount operations, async I/O, journaling, crash recovery, or broad storage/device support.

#### Scenario: 创建嵌套目录并在其中创建多个文件
- **WHEN** a user process creates `/rw/a`, creates `/rw/a/b`, and creates multiple regular files under `/rw/a/b` within configured capacity limits
- **THEN** BigOS MUST make all successfully created directories and files visible through subsequent lookup, `stat`/`fstat`, open/read/write, and bounded directory enumeration in the same boot session
- **AND** the created objects MUST carry deterministic metadata consistent with the caller identity and requested mode within the existing bounded metadata contract

#### Scenario: 路径中间组件必须是目录
- **WHEN** a user process attempts to create or lookup `/rw/file/child` where `/rw/file` is a regular file
- **THEN** BigOS MUST reject the operation with a deterministic not-directory error
- **AND** it MUST NOT create a child object, publish a partial fd, or modify unrelated directory entries

#### Scenario: 目录树容量耗尽不发布半成品
- **WHEN** directory or file creation requires an inode, directory slot, data block, cache block, or kernel allocation that is unavailable
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** later lookup, metadata query, directory enumeration, and fd operations MUST observe the pre-failure state

### Requirement: 空目录删除
BigOS SHALL support removal of empty directories under `/rw` through a bounded `SYS_RMDIR` directory-removal operation. Directory removal MUST require a writable parent directory, an existing directory target, and an empty target directory. It MUST reject non-empty directories, regular files, missing paths, read-only backend paths, and unsupported object types with deterministic errors. Removing a directory MUST NOT remove regular files recursively, MUST NOT be performed through `SYS_UNLINK`, and MUST NOT reorder existing syscall numbers.

#### Scenario: 删除空目录成功
- **WHEN** a user process removes an existing empty directory under `/rw` and has permission to mutate the parent directory
- **THEN** BigOS MUST remove the directory entry from the parent directory
- **AND** subsequent lookup, metadata query, and directory enumeration MUST no longer return that directory
- **AND** the directory inode and owned data blocks MUST become reusable after no live references remain

#### Scenario: 删除非空目录被拒绝
- **WHEN** a user process attempts to remove a directory that still contains at least one live child entry
- **THEN** BigOS MUST return a deterministic not-empty error
- **AND** it MUST NOT remove any child entry, parent entry, inode metadata, or data block

#### Scenario: 常规文件不能通过目录删除入口删除
- **WHEN** a user process invokes the directory-removal operation on a regular file
- **THEN** BigOS MUST return a deterministic not-directory or invalid-object-type error
- **AND** the regular file MUST remain visible and readable according to its existing permissions

### Requirement: 目录树引用与删除语义
BigOS SHALL keep deletion behavior explainable when directory tree objects are referenced by open files, open directories, current working directories, or inherited process state. Regular file deletion MUST continue to remove the path entry while preserving already-open fd access until the last reference closes. Directory deletion MUST support deleted-directory cwd semantics: a removed directory entry may disappear from its parent while live cwd/open-directory references keep the directory object valid until the last reference closes.

#### Scenario: 删除已打开常规文件后 fd 仍有效
- **WHEN** a process opens `/rw/a/file`, unlinks `/rw/a/file`, and continues using the already-open fd
- **THEN** subsequent path lookup MUST report the path as missing
- **AND** the open fd MUST remain usable according to its original access mode until closed
- **AND** inode and data-block reclamation MUST wait until the final open reference is released

#### Scenario: 删除被 cwd 引用的目录进入 deleted 状态
- **WHEN** a process or related process has `/rw/a/b` as current working directory and another process attempts to remove `/rw/a/b`
- **THEN** BigOS MUST remove `/rw/a/b` from new parent-directory lookup only if `/rw/a/b` is empty and all permission/type checks pass
- **AND** existing cwd or open-directory references MUST enter a deleted-directory state that remains valid until the references are released
- **AND** subsequent `getcwd`, relative lookup, and `chdir("..")` from that deleted-directory state MUST return deterministic results or documented errors
- **AND** BigOS MUST NOT leave a dangling cwd pointer, reuse the deleted directory object for a new same-name directory, or free the directory inode/data blocks before the last live reference closes

### Requirement: 目录树枚举一致性
BigOS SHALL make bounded directory enumeration observe successful directory tree mutations under `/rw`. Enumeration MUST return at least entry name and basic type for live regular-file and directory entries within the configured batch limit. Enumeration MUST reject non-directory fd, invalid fd, invalid user buffers, overlarge requested batches, and unsupported backends with deterministic errors. It MUST NOT promise POSIX ordering, stable snapshots, `.`/`..` entries, complete offset-cookie semantics, or full `struct dirent` compatibility.

#### Scenario: 枚举看到新建目录和多个文件
- **WHEN** a user process creates a directory and multiple regular files under `/rw/a`
- **THEN** bounded enumeration of `/rw/a` MUST include the created live entries within the configured output bounds
- **AND** each returned entry MUST identify whether it is a regular file or directory

#### Scenario: 删除后枚举不再显示目录项
- **WHEN** a regular file is successfully unlinked or an empty directory is successfully removed
- **THEN** bounded enumeration of the parent directory MUST no longer return the removed entry name
- **AND** unrelated entries in the parent directory MUST remain visible

### Requirement: 只读后端隔离
BigOS SHALL keep read-only exFAT boot assets isolated from `/rw` directory tree mutations. Directory creation, file creation, unlink, directory removal, and rename-like directory tree mutation requests targeting the read-only backend or crossing between read-only and writable backends MUST fail deterministically without modifying read-only state.

#### Scenario: 只读路径拒绝目录树变更
- **WHEN** a user process attempts to create, remove, or mutate a path under the read-only boot asset backend
- **THEN** BigOS MUST return a deterministic read-only filesystem error
- **AND** existing boot assets MUST remain readable and unchanged

#### Scenario: 跨后端目录树变更被拒绝
- **WHEN** a mutation would move, rename, or otherwise connect an object between `/rw` and the read-only boot asset backend
- **THEN** BigOS MUST reject the operation with a deterministic read-only or unsupported-cross-backend error
- **AND** both source and destination backend states MUST remain unchanged
