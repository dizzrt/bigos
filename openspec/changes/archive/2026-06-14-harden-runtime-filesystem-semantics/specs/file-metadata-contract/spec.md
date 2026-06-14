## ADDED Requirements

### Requirement: 元数据与运行时文件状态一致

BigOS SHALL make bounded path and fd metadata queries reflect successful runtime filesystem operations on supported backends while preserving the existing bounded metadata subset. Metadata results MUST be consistent with object type, size, mode, uid, gid, path visibility, and open file reference lifetime after successful create, write, truncate, mkdir, unlink, and restricted regular-file rename operations. Metadata queries MUST NOT expose stable inode identity, complete timestamp semantics, ACLs, extended attributes, device nodes, symlink state, or cross-reboot persistence.

#### Scenario: 写入和截断后 metadata size 更新

- **WHEN** 用户进程在 `/rw` 中成功创建、写入或截断常规文件，并随后通过路径或有效 fd 查询 metadata
- **THEN** BigOS MUST report bounded metadata whose type and size match the successful runtime file state
- **AND** unsupported metadata fields MUST remain zero or documented bounded defaults

#### Scenario: 目录 metadata 反映目录对象存在性

- **WHEN** 用户进程在 `/rw` 中成功 mkdir、rename 或 unlink 后查询相关目录或目录项 metadata
- **THEN** path metadata query MUST reflect the current visible directory tree
- **AND** missing or removed paths MUST return deterministic not-found errors without publishing partial metadata

### Requirement: 路径 metadata 与 fd metadata 区分删除和 rename 引用

BigOS SHALL distinguish path-visible metadata from fd-referenced metadata when directory entries are removed or renamed while open file objects still exist. Path metadata queries MUST follow current directory entry visibility; fd metadata queries MUST follow the live open file object until its reference count reaches zero. This behavior MUST remain bounded and MUST NOT imply stable inode numbers or persistent object identity.

#### Scenario: unlink 后路径查询失败但 fd 查询有效

- **WHEN** 用户进程打开 `/rw` 常规文件、成功 unlink 其路径，并在 fd 关闭前分别执行路径 metadata 查询和 fd metadata 查询
- **THEN** path metadata query for the removed path MUST fail as missing
- **AND** fd metadata query for the still-open descriptor MUST return bounded metadata for the referenced object

#### Scenario: rename 后新路径查询有效且旧路径失败

- **WHEN** 用户进程打开 `/rw` 常规文件并成功将其路径 rename 到同一可写后端的新名称
- **THEN** metadata query for the new path MUST report the renamed object
- **AND** metadata query for the old path MUST fail as missing
- **AND** fd metadata query for the pre-rename open descriptor MUST remain valid until close

### Requirement: 元数据错误边界对用户态稳定

BigOS SHALL return deterministic negative errno values for metadata query failures caused by missing paths, invalid fd, unsupported object type, permission denial, illegal user buffers, backend IO errors, nonblocking context, or unsupported path forms. Failed metadata queries MUST NOT mutate fd offsets, cwd, directory entries, cache dirty state, or user output buffers with partial/uninitialized data.

#### Scenario: metadata 查询失败不推进 offset

- **WHEN** 用户进程在 read、write 或 lseek 前后执行 path or fd metadata query that fails
- **THEN** BigOS MUST return a deterministic error
- **AND** MUST NOT change the open file offset or unrelated fd table state

#### Scenario: 非法用户缓冲不泄漏内核数据

- **WHEN** 用户进程为 metadata query 提供 unmapped、read-only、kernel-space 或 otherwise invalid destination buffer
- **THEN** BigOS MUST return a deterministic fault error
- **AND** MUST NOT copy partial metadata or uninitialized kernel memory to user space
