## ADDED Requirements

### Requirement: fd close-on-exec control

BigOS SHALL extend the process fd table and fd/VFS syscall surface with bounded close-on-exec control. The fd table MUST store close-on-exec state per descriptor entry, expose deterministic query/set operations, and preserve open file object lifetime, offset state, and backend references.

#### Scenario: fd-control set updates descriptor entry

- **WHEN** a process sets close-on-exec on a valid descriptor
- **THEN** fd/VFS MUST update only that descriptor entry's close-on-exec flag
- **AND** it MUST NOT close the descriptor, change file offset, mutate backend file content, or alter other descriptor entries

#### Scenario: fd-control get observes descriptor entry

- **WHEN** a process queries close-on-exec on a valid descriptor
- **THEN** fd/VFS MUST return the descriptor entry's current close-on-exec state
- **AND** the query MUST be side-effect free

### Requirement: bounded access and metadata through VFS

BigOS SHALL expose bounded path and fd metadata checks through the existing VFS path resolution and file metadata contract. Path-taking variants MUST support absolute and cwd-relative paths, including supported `.` and `..` handling, while rejecting overlong paths, invalid user buffers, deleted cwd state, unsupported mode bits, read-only mutation attempts, and backend-specific failures deterministically.

#### Scenario: access 使用共享路径解析

- **WHEN** a process checks bounded access for an absolute or cwd-relative path
- **THEN** fd/VFS MUST resolve the target through the same path helper used by open, metadata, directory, and writable backend operations
- **AND** it MUST return deterministic success or errno without opening a published descriptor

#### Scenario: metadata query copies bounded structure

- **WHEN** a process requests metadata for a valid path or open descriptor
- **THEN** fd/VFS MUST fill the bounded metadata structure from the existing backend metadata contract
- **AND** invalid output buffers, bad descriptors, missing paths, and unsupported backend states MUST fail without copying partial untrusted data as a successful result

### Requirement: truncate 类操作统一语义

BigOS SHALL provide consistent bounded semantics for truncating writable regular files by path or descriptor. Truncation MUST be routed through the writable backend and page/buffer-cache writeback model, reject unsupported targets deterministically, and preserve current read-only exFAT behavior.

#### Scenario: path truncate writable regular file

- **WHEN** a process truncates an existing writable `/rw` regular file by path within supported size limits
- **THEN** fd/VFS MUST route the request to the writable backend, update the file size, and make subsequent reads observe the new bounded file length
- **AND** failures MUST leave the previously committed file size observable

#### Scenario: ftruncate rejects unsupported descriptors

- **WHEN** a process truncates a descriptor that is closed, read-only, directory-backed, pipe-backed, or otherwise unsupported
- **THEN** fd/VFS MUST return a deterministic errno
- **AND** it MUST NOT mutate backend state or fd table ownership
