## ADDED Requirements

### Requirement: 可写后端权限与提交前检查稳定

BigOS SHALL enforce RAM-backed `/rw` owner/mode, directory writability, object type, capacity, and backend mutability checks before committing file data, inode metadata, directory entries, cache dirty state, or open file offsets. Failed checks MUST return deterministic negative errno values and MUST leave existing runtime filesystem state explainable from the caller's perspective. Permission failures MUST use the existing minimal owner/mode access checks and stable `-EACCES` behavior; this change MUST NOT define complete POSIX directory execute/search bit traversal semantics. This requirement MUST NOT introduce ACLs, extended attributes, full POSIX permission databases, persistent writable storage, or journaling.

#### Scenario: 权限拒绝不修改状态

- **WHEN** 用户进程对 `/rw` 文件或目录执行 create、open-for-write、truncate、write、mkdir、unlink、rename 或 fsync，但 owner/mode 或目录权限检查拒绝该操作
- **THEN** BigOS MUST 返回稳定的 `-EACCES`
- **AND** MUST NOT 修改文件内容、目录项、inode metadata、fd offset、dirty block 状态或目录枚举可见结果
- **AND** MUST NOT require complete POSIX directory execute/search bit semantics as part of this change

#### Scenario: 容量耗尽不发布半成品对象

- **WHEN** `/rw` 操作需要分配 inode、目录项、数据块、缓存块或内核对象但容量耗尽
- **THEN** BigOS MUST 返回确定性容量或内存错误
- **AND** MUST NOT 发布半成品文件、目录、rename 目标、truncate 结果或部分 metadata 更新

### Requirement: 可写后端运行期一致性跨操作可观察

BigOS SHALL make successful `/rw` runtime filesystem changes visible to subsequent operations in the same boot session. Successful create, write, truncate, fsync, mkdir, unlink, and restricted regular-file rename operations MUST produce consistent results across read, open, stat/fstat, minimal directory enumeration, dup/fork/exec-inherited fd references, and independent path reopen. The guarantee MUST remain limited to the current runtime session and MUST NOT imply cross-reboot persistence.

#### Scenario: 成功写入和截断反映到 read 与 stat

- **WHEN** 用户进程在 `/rw` 中创建文件、写入内容、截断或追加，并随后通过同一 fd、dup 后 fd、继承 fd 或重新打开路径读取和查询 metadata
- **THEN** BigOS MUST expose the successful runtime file contents and bounded metadata consistently
- **AND** independent opens MUST retain independent offsets while dup-related fds share the same open file offset

#### Scenario: 目录变更反映到路径查找和枚举

- **WHEN** 用户进程在 `/rw` 中成功执行 mkdir、unlink 或 restricted regular-file rename
- **THEN** later path lookup, open, metadata query, and minimal directory enumeration MUST observe the resulting directory state
- **AND** removed or renamed source paths MUST no longer be found unless still represented by an existing open fd reference

#### Scenario: rename 目标已存在保守失败

- **WHEN** 用户进程在 `/rw` 中执行 restricted regular-file rename，且目标路径已存在并且不是同一父目录同一名称 no-op
- **THEN** BigOS MUST return `-EEXIST`
- **AND** MUST NOT replace the target object, remove the source object, alter open file references, or claim POSIX atomic replacement semantics

#### Scenario: 运行期一致性不承诺重启后保留

- **WHEN** documentation, validation, user tools, or specs describe `/rw` file contents after successful writes or syncs
- **THEN** they MUST describe the guarantee as current-runtime consistency over the RAM-backed writable backend
- **AND** MUST NOT claim persistence after reboot or imply modification of the existing exFAT boot image layout

### Requirement: 只读与可写后端差异保持隔离

BigOS SHALL preserve strict isolation between read-only exFAT boot assets and the RAM-backed `/rw` writable backend. Writable operations MUST be accepted only when the resolved target belongs to the supported writable backend and all bounded checks pass. Read-only metadata queries and reads MUST NOT require or create writable state.

#### Scenario: exFAT 查询和读取不产生可写副作用

- **WHEN** 用户进程读取或查询只读 exFAT 路径的 metadata
- **THEN** BigOS MUST return the bounded read or metadata result without allocating writable backend objects, dirtying cache blocks for exFAT state, or modifying boot assets

#### Scenario: `/rw` 失败不影响 exFAT

- **WHEN** `/rw` operation fails due to permission, capacity, path, object type, user buffer, or IO error
- **THEN** BigOS MUST preserve read-only exFAT mount state and future exFAT path reads
- **AND** MUST NOT use exFAT state as rollback storage or persistence backing for `/rw`
