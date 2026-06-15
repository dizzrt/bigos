## ADDED Requirements

### Requirement: metadata 反映运行期文件系统变化
BigOS SHALL make bounded path and fd metadata queries reflect successful runtime filesystem changes on supported backends. Metadata MUST report object type, size, mode, uid, gid, and documented bounded defaults consistently after successful `/rw` create, write, truncate, mkdir, unlink, restricted regular-file rename, and read-only exFAT metadata queries. Metadata MUST NOT expose stable inode identity, complete timestamp semantics, ACLs, xattrs, device nodes, symlink state, or cross-reboot persistence.

#### Scenario: 写入后 metadata size 更新
- **WHEN** a user process creates or opens a `/rw` regular file, successfully writes or truncates it, and then queries metadata by path or valid fd
- **THEN** BigOS MUST report bounded metadata whose object type and size match the successful current-runtime file state
- **AND** unsupported metadata fields MUST remain zero or documented bounded defaults

#### Scenario: 目录 metadata 反映当前可见树
- **WHEN** a user process successfully creates, removes, or restricted-renames entries under `/rw` and then queries metadata for affected paths
- **THEN** BigOS MUST return metadata for currently visible paths and deterministic not-found errors for removed or old renamed paths
- **AND** it MUST NOT publish partial or uninitialized metadata on failure

### Requirement: path metadata 与 fd metadata 区分目录项可见性
BigOS SHALL distinguish path-visible metadata from fd-referenced metadata when directory entries are unlinked or renamed while open file objects still exist. Path metadata queries MUST follow current directory entry visibility; fd metadata queries MUST follow the live open file object until its reference count reaches zero. This behavior MUST remain bounded and MUST NOT imply stable inode numbers or persistent object identity.

#### Scenario: unlink 后路径查询失败但 fd 查询有效
- **WHEN** a user process opens a `/rw` regular file, successfully unlinks its path, and then queries metadata for both the removed path and the still-open fd
- **THEN** path metadata query for the removed path MUST fail as missing
- **AND** fd metadata query for the still-open descriptor MUST return bounded metadata for the referenced open object

#### Scenario: rename 后新路径查询有效且旧路径失败
- **WHEN** a user process opens a `/rw` regular file and successfully restricted-renames its path to a new name in the same writable backend
- **THEN** metadata query for the new path MUST report the renamed object
- **AND** metadata query for the old path MUST fail as missing
- **AND** fd metadata query for the pre-rename descriptor MUST remain valid until close

### Requirement: metadata 失败不改变文件状态
BigOS SHALL return deterministic negative errno values for metadata query failures caused by missing paths, invalid fd, unsupported object type, permission denial, illegal user buffers, backend I/O errors, nonblocking context, or unsupported path forms. Failed metadata queries MUST NOT mutate fd offsets, cwd, directory entries, cache dirty state, or user output buffers with partial or uninitialized data.

#### Scenario: metadata 查询失败不推进 offset
- **WHEN** a user process performs path or fd metadata query before or after read, write, or lseek operations and the metadata query fails
- **THEN** BigOS MUST return a deterministic error
- **AND** it MUST NOT change the open file offset, fd table, cwd, directory entries, or cache state

#### Scenario: 非法用户缓冲不泄漏内核数据
- **WHEN** a user process supplies an unmapped, read-only, kernel-space, or otherwise invalid destination buffer for metadata
- **THEN** BigOS MUST return a deterministic fault error
- **AND** it MUST NOT copy partial metadata or uninitialized kernel memory to user space
