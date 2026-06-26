## Purpose

定义 BigOS 最小文件与目录元数据契约，覆盖内核 fd/VFS 查询、freestanding libc 暴露、用户态消费和行为验证。该能力只承诺当前内核与文件系统后端支持的有界 `stat`/`fstat` 风格字段，不引入完整 POSIX metadata database、符号链接、设备节点、ACL、xattr、纳秒级或完整 POSIX 时间戳语义、稳定 inode 身份或跨重启持久化承诺。
## Requirements
### Requirement: 最小元数据结构

BigOS SHALL define a bounded file metadata structure for user-visible `stat`/`fstat` style queries. The structure MUST use fixed-width fields, MUST be fully initialized before copying to user memory, and MUST expose only the bounded subset supported by the current kernel and filesystem backends: object type, size, mode, uid, gid, bounded link count or default value, user-visible object identifier fixed to zero in the first version, bounded timestamp fields `atime`, `mtime`, and `ctime` expressed as Unix epoch seconds, and explicit reserved zero fields. The structure MUST NOT imply complete POSIX `struct stat` compatibility, device-node semantics, symbolic links, ACLs, extended attributes, nanosecond timestamp precision, timezone conversion, complete POSIX timestamp semantics, stable inode numbers, or persistent inode identity.

#### Scenario: 常规文件元数据字段有界

- **WHEN** a caller queries metadata for an existing regular file on a supported backend
- **THEN** BigOS MUST return a fully initialized metadata structure with regular-file type, bounded file size, mode, uid and gid values derived from the backend contract or documented defaults
- **AND** timestamp fields MUST be initialized to backend-provided values or documented bounded defaults
- **AND** unsupported fields MUST be zero or documented bounded defaults rather than uninitialized data

#### Scenario: 目录元数据字段有界

- **WHEN** a caller queries metadata for an existing directory on a supported backend
- **THEN** BigOS MUST return directory type and bounded directory metadata without requiring complete directory traversal, `.`/`..`, symbolic links, mount namespaces, or POSIX directory database semantics

#### Scenario: 未支持元数据不扩大兼容承诺

- **WHEN** documentation, headers, or user tools describe the metadata structure
- **THEN** they MUST describe it as a BigOS bounded metadata subset
- **AND** they MUST NOT claim full POSIX `stat` compatibility, full permission database semantics, persistent inode identity, nanosecond timestamp precision, timezone conversion, or complete POSIX timestamp support

#### Scenario: 对象编号第一版保持零值

- **WHEN** a caller queries metadata for any supported object on exFAT or `/rw`
- **THEN** BigOS MUST return zero in the user-visible object identifier field
- **AND** it MUST NOT expose `/rw` runtime inode numbers or exFAT backend identifiers as stable user ABI in this version

### Requirement: metadata timestamp ABI 镜像一致

BigOS SHALL keep kernel metadata and user-visible libc metadata timestamp fields ABI-compatible. The kernel metadata structure and `struct stat` mirror MUST expose `atime`, `mtime`, and `ctime` fields with matching field order, signedness/width assumptions, and copy-to-user initialization behavior. The ABI MUST NOT expose nanosecond subfields, timezone conversion state, locale formatting state, or unsupported POSIX metadata fields as implemented behavior.

#### Scenario: 内核和用户结构字段一致

- **WHEN** source contract validation checks the kernel metadata structure, syscall copy path, and user `struct stat`
- **THEN** the timestamp fields MUST appear in a consistent ABI order with fixed-width storage
- **AND** successful metadata queries MUST fully initialize those fields before publication to user memory

### Requirement: 路径元数据查询

BigOS SHALL provide a path-taking metadata query that resolves a bounded NUL-terminated path through the current VFS and returns metadata for the resolved object. Absolute paths MUST resolve from root; relative paths MUST resolve from the current process cwd and support POSIX-style `.`/`..` components within the supported BigOS directory tree. The operation MUST preserve the bounded metadata contract, MUST reject unsupported path forms deterministically, and MUST NOT introduce symbolic-link traversal, mount namespaces, `chroot`, or complete `realpath` semantics.

#### Scenario: 绝对路径查询成功

- **WHEN** a user process queries metadata for an existing absolute path on the read-only exFAT backend or RAM-backed `/rw` backend
- **THEN** BigOS MUST resolve the path through VFS and return metadata matching the resolved object type and size within the bounded metadata contract

#### Scenario: 相对路径查询成功

- **WHEN** 当前进程 cwd 指向支持的目录且用户进程查询一个存在对象的相对路径
- **THEN** BigOS MUST resolve the path from cwd through VFS
- **AND** MUST return metadata matching the cwd-resolved object within the bounded metadata contract

#### Scenario: dot-dot 相对路径查询成功

- **WHEN** 当前进程 cwd 为 `/rw/work/sub` 且用户进程查询 `../note.txt`
- **THEN** BigOS MUST resolve the query as `/rw/work/note.txt`
- **AND** MUST return metadata for that resolved object within the bounded metadata contract

#### Scenario: 缺失路径查询失败

- **WHEN** a user process queries metadata for a missing absolute path or cwd-resolved relative path
- **THEN** BigOS MUST return a deterministic not-found error
- **AND** it MUST NOT publish partially initialized metadata to the user buffer

#### Scenario: 不支持路径形式被拒绝

- **WHEN** a user process queries metadata using an overlong path, non-NUL-terminated path, unsupported relative path form outside the documented `.`/`..` component contract, or otherwise unsupported path form
- **THEN** BigOS MUST fail deterministically
- **AND** it MUST NOT add symlink, mount namespace, `chroot`, or complete `realpath` behavior as a side effect

### Requirement: fd 元数据查询

BigOS SHALL provide an fd-taking metadata query that returns metadata for the open file object referenced by a valid descriptor in the current process fd table. The operation MUST respect fd table lifetime, dup/dup2 shared references, inherited descriptors, directory objects already represented by existing open file object or minimal directory-enumeration paths, and pipe or non-regular object boundaries where applicable. This requirement MUST NOT introduce generic open-directory semantics solely for metadata queries.

#### Scenario: 有效 fd 查询成功

- **WHEN** a user process queries metadata for a valid fd referring to a regular file or directory
- **THEN** BigOS MUST return metadata for the referenced open file object without changing its file offset
- **AND** duplicated fds referring to the same open file object MUST report metadata for the same object

#### Scenario: 非法 fd 被拒绝

- **WHEN** a user process queries metadata for an unused, closed, negative, or out-of-range fd
- **THEN** BigOS MUST return a deterministic bad-fd error
- **AND** it MUST NOT access freed file state or mutate the process fd table

#### Scenario: 查询不推进 offset

- **WHEN** a process calls fd metadata query before or after read, write, or lseek operations
- **THEN** the query MUST NOT change the open file offset
- **AND** subsequent file I/O MUST observe the same offset it would have observed without the metadata query

### Requirement: 后端元数据一致性

BigOS SHALL expose metadata consistently across the read-only exFAT backend and RAM-backed `/rw` backend while preserving each backend's existing storage boundary. Read-only exFAT metadata MUST NOT require writable state, and `/rw` metadata MUST reflect successful runtime create, write, truncate, mkdir, unlink, and permission metadata changes according to the existing bounded writable filesystem semantics. The contract MUST NOT promise cross-reboot persistence for `/rw`.

#### Scenario: 只读后端查询不修改状态

- **WHEN** a user process queries metadata for an object on the read-only exFAT backend
- **THEN** BigOS MUST return metadata without modifying filesystem state, block contents, boot image layout, MBR, partition data, or exFAT discovery behavior

#### Scenario: 可写后端反映运行期变化

- **WHEN** a user process creates, writes, truncates, or changes directory entries within `/rw` and then queries metadata for the affected object
- **THEN** BigOS MUST report metadata consistent with the successful runtime filesystem operation
- **AND** the result MUST NOT imply persistence after reboot

#### Scenario: 删除后路径和 fd 语义区分

- **WHEN** a `/rw` regular file is opened, unlinked by path, and still has an open fd reference
- **THEN** path metadata query for the removed path MUST fail as missing
- **AND** fd metadata query for the still-open fd MUST remain valid until the open file reference is closed

### Requirement: 用户缓冲和错误边界

BigOS SHALL copy metadata results to user memory only after validating the destination range through the existing user-memory boundary. Failed path resolution, fd lookup, allocation, backend metadata retrieval, or user-buffer validation MUST return deterministic negative errno values, MUST NOT panic in normal process context, and MUST NOT leak uninitialized kernel memory.

#### Scenario: 用户缓冲非法被拒绝

- **WHEN** a user process supplies an unmapped, read-only, kernel, or otherwise invalid destination buffer for a metadata query
- **THEN** BigOS MUST return a deterministic fault error
- **AND** it MUST NOT write partial metadata or leak uninitialized kernel data

#### Scenario: 后端失败不发布结果

- **WHEN** backend metadata retrieval fails because of allocation failure, unsupported object type, storage error, or inconsistent filesystem state
- **THEN** BigOS MUST return a deterministic error
- **AND** it MUST NOT publish partially initialized metadata to the caller

#### Scenario: 不可阻塞上下文不执行查询

- **WHEN** metadata query code would be invoked from IRQ context, scheduler critical sections, preemption-disabled nonblocking regions, or another context where blocking and allocation are not allowed
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT perform blocking disk I/O, unbounded allocation, or wait-queue operations from that context

### Requirement: libc 元数据接口

BigOS userland libc SHALL expose bounded `stat`/`fstat` style wrappers, public types, and constants for simple static C programs. The wrappers MUST follow the existing user syscall ABI, translate kernel negative errno returns into positive user `errno` plus the documented failure sentinel, and leave successful results in the caller-provided metadata structure. Headers MUST remain freestanding-safe and MUST NOT depend on hosted libc, dynamic linking, shared libraries, threads, locale, or complete POSIX libc.

#### Scenario: libc wrapper 成功返回

- **WHEN** a simple static C program calls the metadata wrapper for an existing path or valid fd
- **THEN** libc MUST invoke the kernel syscall using the existing register convention
- **AND** it MUST return success with a filled bounded metadata structure visible to the caller

#### Scenario: libc wrapper 失败设置 errno

- **WHEN** the kernel returns a negative errno for a metadata query
- **THEN** libc MUST set user `errno` to the corresponding positive value
- **AND** it MUST return the documented failure sentinel without requiring the program to parse kernel negative errno values

#### Scenario: 头文件不声明未实现接口

- **WHEN** user programs include BigOS metadata headers
- **THEN** the headers MUST declare only the bounded metadata structure, supported constants, and implemented wrappers
- **AND** they MUST NOT declare complete hosted `stat` families, dynamic loader behavior, thread-safe metadata APIs, or unsupported POSIX database interfaces

### Requirement: 用户态消费路径

BigOS SHALL make metadata naturally observable from the bounded userland through a small packaged `stat`-style user program launched from the shell or validation path. The output MUST be deterministic enough for behavior checks and manual inspection, and the consumption path MUST NOT expand into a complete POSIX utility suite, full POSIX `stat`, full `ls -l`, globbing, scripting language, job control, terminal process groups, or complete shell semantics.

#### Scenario: 用户工具展示文件元数据

- **WHEN** a user runs the metadata consumption path for an existing file or directory
- **THEN** BigOS userland MUST display deterministic bounded metadata including at least object type and size
- **AND** optional mode, uid, gid, and backend default fields MUST be displayed consistently when included

#### Scenario: 用户工具报告确定性错误

- **WHEN** a user runs the metadata consumption path for a missing path, invalid fd, unsupported path form, or permission-denied object
- **THEN** the tool or builtin MUST report a deterministic error using libc errno behavior
- **AND** the shell MUST remain in its bounded read-parse-execute loop after the error

#### Scenario: 用户工具范围保持有界

- **WHEN** user-visible metadata tooling is documented or validated
- **THEN** it MUST be described as a small BigOS `stat`-style utility for metadata observation
- **AND** it MUST NOT be described as a complete POSIX `stat`, complete `ls`, globbing engine, or scripting environment

### Requirement: 元数据行为验证

BigOS SHALL provide behavior-oriented validation for the metadata contract. Validation MUST cover kernel-visible behavior, libc wrapper behavior, userland consumption, successful and failing queries, read-only and writable backend differences, and user-buffer failure paths. Environment-dependent emulator checks MUST record unavailable QEMU, Bochs, cross-toolchain, ROM/display, image, or timeout dependencies as skipped rather than passed.

#### Scenario: 行为断言覆盖成功路径

- **WHEN** metadata validation runs in an environment with the required build and runtime support
- **THEN** it MUST verify metadata queries for at least one read-only backend file, one `/rw` regular file, and one directory
- **AND** it MUST check that object type and size are observable through the bounded metadata contract

#### Scenario: 行为断言覆盖失败路径

- **WHEN** metadata validation runs
- **THEN** it MUST verify deterministic failures for missing path, unsupported path form, invalid fd, and invalid user buffer cases
- **AND** it MUST verify that failed queries do not mutate fd offsets or publish partial metadata

#### Scenario: 环境不可用时记录跳过

- **WHEN** emulator, cross-toolchain, disk image, display/ROM, serial oracle, or timeout dependencies are unavailable
- **THEN** validation notes MUST record the skipped dependency and residual risk
- **AND** they MUST NOT claim runtime metadata validation passed

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

### Requirement: 文件增长和截断更新有界 size metadata
BigOS SHALL make bounded path and fd metadata queries reflect successful `/rw` regular-file extension writes and truncate operations. Metadata MUST report the committed file size after append writes, seek-past-EOF writes, cross-block writes, shrink truncates, and extend truncates on supported writable backends. Failed growth or truncate operations MUST leave metadata explainable from the pre-failure state.

#### Scenario: 扩展写后 stat 报告新大小
- **WHEN** a user process successfully extends a `/rw` regular file and then queries metadata by path or valid fd
- **THEN** BigOS MUST report the enlarged bounded file size
- **AND** the reported object type, owner, mode, uid, and gid fields MUST remain consistent with the existing metadata subset

#### Scenario: 截断后 stat 报告提交大小
- **WHEN** a user process successfully shrinks or extends a `/rw` regular file through supported truncate behavior and then queries metadata by path or fd
- **THEN** BigOS MUST report the committed truncated size
- **AND** it MUST NOT report an intermediate size from a failed operation

### Requirement: metadata 查询不暴露块分配细节
BigOS SHALL keep file metadata bounded while file growth and truncate mature. Metadata MAY expose supported file type, size, mode, uid, gid, and documented bounded defaults, but MUST NOT expose raw data block numbers, free-space metadata, stable inode identity beyond existing contracts, allocation generation counters, sparse extent details, journaling state, or crash-recovery status.

#### Scenario: 查询增长文件不返回块布局
- **WHEN** a user process queries metadata for a grown or truncated `/rw` regular file
- **THEN** BigOS MUST return only the supported bounded metadata fields
- **AND** it MUST NOT expose raw block allocation layout or internal free-space state

#### Scenario: 失败增长后 metadata 保持旧状态
- **WHEN** a file growth or truncate attempt fails before publishing the new file state
- **THEN** subsequent metadata queries MUST report the last successfully committed size and supported attributes
- **AND** they MUST NOT reveal partially prepared block allocation state
