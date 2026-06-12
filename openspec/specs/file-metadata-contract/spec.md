## Purpose

定义 BigOS 最小文件与目录元数据契约，覆盖内核 fd/VFS 查询、freestanding libc 暴露、用户态消费和行为验证。该能力只承诺当前内核与文件系统后端支持的有界 `stat`/`fstat` 风格字段，不引入完整 POSIX metadata database、符号链接、设备节点、ACL、xattr、完整时间戳语义、稳定 inode 身份或跨重启持久化承诺。

## Requirements

### Requirement: 最小元数据结构

BigOS SHALL define a bounded file metadata structure for user-visible `stat`/`fstat` style queries. The structure MUST use fixed-width fields, MUST be fully initialized before copying to user memory, and MUST expose only the bounded subset supported by the current kernel and filesystem backends: object type, size, mode, uid, gid, bounded link count or default value, user-visible object identifier fixed to zero in the first version, and explicit reserved zero fields. The structure MUST NOT imply complete POSIX `struct stat` compatibility, device-node semantics, symbolic links, ACLs, extended attributes, complete timestamp semantics, stable inode numbers, or persistent inode identity.

#### Scenario: 常规文件元数据字段有界

- **WHEN** a caller queries metadata for an existing regular file on a supported backend
- **THEN** BigOS MUST return a fully initialized metadata structure with regular-file type, bounded file size, mode, uid and gid values derived from the backend contract or documented defaults
- **AND** unsupported fields MUST be zero or documented bounded defaults rather than uninitialized data

#### Scenario: 目录元数据字段有界

- **WHEN** a caller queries metadata for an existing directory on a supported backend
- **THEN** BigOS MUST return directory type and bounded directory metadata without requiring complete directory traversal, `.`/`..`, symbolic links, mount namespaces, or POSIX directory database semantics

#### Scenario: 未支持元数据不扩大兼容承诺

- **WHEN** documentation, headers, or user tools describe the metadata structure
- **THEN** they MUST describe it as a BigOS bounded metadata subset
- **AND** they MUST NOT claim full POSIX `stat` compatibility, full permission database semantics, persistent inode identity, or complete timestamp support

#### Scenario: 对象编号第一版保持零值

- **WHEN** a caller queries metadata for any supported object on exFAT or `/rw`
- **THEN** BigOS MUST return zero in the user-visible object identifier field
- **AND** it MUST NOT expose `/rw` runtime inode numbers or exFAT backend identifiers as stable user ABI in this version

### Requirement: 路径元数据查询

BigOS SHALL provide a path-taking metadata query that resolves a bounded NUL-terminated absolute path through the current VFS and returns metadata for the resolved object. The operation MUST preserve the current absolute-path-only boundary, MUST reject unsupported path forms deterministically, and MUST NOT introduce relative path resolution, symbolic-link traversal, mount namespaces, `chroot`, or complete canonicalization semantics.

#### Scenario: 绝对路径查询成功

- **WHEN** a user process queries metadata for an existing absolute path on the read-only exFAT backend or RAM-backed `/rw` backend
- **THEN** BigOS MUST resolve the path through VFS and return metadata matching the resolved object type and size within the bounded metadata contract

#### Scenario: 缺失路径查询失败

- **WHEN** a user process queries metadata for a missing absolute path
- **THEN** BigOS MUST return a deterministic not-found error
- **AND** it MUST NOT publish partially initialized metadata to the user buffer

#### Scenario: 不支持路径形式被拒绝

- **WHEN** a user process queries metadata using a relative path, overlong path, non-NUL-terminated path, or otherwise unsupported path form
- **THEN** BigOS MUST fail deterministically
- **AND** it MUST NOT add current-directory, symlink, mount namespace, or complete canonicalization behavior as a side effect

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
