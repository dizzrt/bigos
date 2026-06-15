## ADDED Requirements

### Requirement: `/rw` 运行期文件状态组合一致
BigOS SHALL make successful RAM-backed `/rw` filesystem changes visible to later operations in the same boot session. Successful create, open, write, truncate, lseek, fsync, mkdir, unlink, restricted regular-file rename, stat/fstat, and minimal directory enumeration operations MUST produce consistent results across same-fd access, dup-shared fd access, fork/exec-inherited fd access, independent path reopen, path metadata, fd metadata, and shell-visible tools. This requirement MUST NOT claim cross-reboot persistence.

#### Scenario: 写入和 truncate 对 read 与 metadata 可见
- **WHEN** a user process creates a `/rw` file, writes data, truncates or extends it through supported open flags or writes, and then reads or queries metadata through the same fd, a dup-shared fd, an inherited fd, or a newly opened path
- **THEN** BigOS MUST expose the successful current-runtime file contents and bounded size metadata consistently
- **AND** dup-related fds MUST share open file offset while independent opens use independent offsets

#### Scenario: 目录变更对 lookup 和 list 可见
- **WHEN** a user process successfully creates a directory, creates a file, unlinks a file, or restricted-renames a regular file under `/rw`
- **THEN** later path lookup, open, metadata query, and minimal directory enumeration MUST observe the resulting current-runtime directory state
- **AND** removed or renamed source paths MUST no longer be found except through existing open fd references
- **AND** minimal directory enumeration MUST report visible entries in stable `/rw` directory slot order for the same unchanged directory state

### Requirement: `/rw` 权限和容量失败不污染状态
BigOS SHALL check owner/mode access, directory writability, object type, path length, file size limit, inode availability, data block availability, directory entry availability, cache block availability, and kernel allocation availability before publishing filesystem mutations. Failed `/rw` operations MUST return deterministic negative errno values and MUST NOT publish partial file data, inode metadata, directory entries, dirty cache state, or fd offset changes that callers could observe as a successful operation.

#### Scenario: 权限拒绝保持旧状态
- **WHEN** a user process attempts create, open-for-write, truncate, write, mkdir, unlink, restricted rename, or fsync under `/rw` but owner/mode or parent directory writability checks reject the operation
- **THEN** BigOS MUST return a stable permission-denied error
- **AND** it MUST NOT modify file contents, directory entries, inode metadata, open file offsets, dirty cache state, or directory enumeration results

#### Scenario: 容量耗尽不发布半成品
- **WHEN** a `/rw` operation needs an inode, data block, directory slot, cache block, or kernel allocation but the relevant bounded capacity is exhausted
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** it MUST NOT publish a partially initialized file, directory, rename target, truncate result, or metadata update

#### Scenario: 自然填满触发容量边界
- **WHEN** a validation path uses ordinary `/rw` filesystem operations to fill the RAM-backed backend until inode, directory slot, or data block capacity is exhausted
- **THEN** BigOS MUST return deterministic capacity errors at the real bounded backend limit
- **AND** it MUST keep previously committed files, directories, metadata, and directory enumeration results consistent

### Requirement: `/rw` 同步语义限于当前 RAM-backed 后端
BigOS SHALL make `fsync`, cache write-back, and cache eviction preserve current-runtime consistency for the RAM-backed `/rw` backend. A successful `fsync` MUST make dirty cached blocks written to the RAM-backed block device such that later cache eviction and reread in the same boot session observe the synchronized content. This requirement MUST NOT imply persistence across reboot or writes to the existing disk image.

#### Scenario: fsync 后淘汰再读一致
- **WHEN** a user process writes a `/rw` file, calls `fsync` successfully, causes or simulates relevant cache eviction, and then reads the file again in the same boot session
- **THEN** BigOS MUST return the synchronized file contents
- **AND** it MUST preserve read-only exFAT state and existing boot image layout

#### Scenario: fsync 失败不静默丢失脏数据
- **WHEN** `fsync` or cache write-back fails for the RAM-backed writable backend
- **THEN** BigOS MUST return a deterministic error
- **AND** it MUST NOT silently report success after discarding dirty data that should remain pending or explainable
