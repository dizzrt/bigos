## ADDED Requirements

### Requirement: fd/VFS 运行期组合语义稳定
BigOS SHALL make fd/VFS path-taking and fd-taking filesystem operations behave consistently across read-only exFAT and RAM-backed `/rw`. Operations including open, read, write, lseek, fsync, mkdir, unlink, restricted regular-file rename, stat, fstat, and minimal directory enumeration MUST share bounded path resolution, backend dispatch, fd lifecycle, and stable errno mapping. The contract MUST NOT introduce mount namespaces, symlink traversal, async I/O, broad file-backed mappings, or complete POSIX VFS semantics.

#### Scenario: 组合操作按解析后端执行
- **WHEN** a user process performs a sequence of path-taking and fd-taking filesystem operations against a read-only exFAT target or a `/rw` target
- **THEN** fd/VFS MUST first resolve the target through the shared bounded path contract and then apply the resolved backend's read-only or writable semantics
- **AND** equivalent absolute and cwd-relative targets MUST produce equivalent backend behavior

#### Scenario: 失败错误对用户态稳定
- **WHEN** an fd/VFS operation fails because of missing path, existing target, invalid fd, invalid user buffer, read-only backend write, unsupported object type, unsupported seek/enumeration, permission denial, capacity exhaustion, allocation failure, or backend I/O failure
- **THEN** fd/VFS MUST return a deterministic negative errno through the syscall layer
- **AND** libc wrappers and shell-visible tools MUST be able to report the failure without parsing internal backend names

### Requirement: open file 引用跨目录变更稳定
BigOS SHALL preserve open file object references across unlink and restricted regular-file rename within `/rw`. Existing fd references, duplicated fd references, and inherited fd references MUST continue to follow the live open file object until its reference count reaches zero, while new path lookups follow current directory entry visibility.

#### Scenario: unlink 后 fd 仍引用打开对象
- **WHEN** a process opens a `/rw` regular file, unlinks its path, and then reads, writes, seeks, or fstats the still-open fd
- **THEN** fd/VFS MUST keep the open file object valid until the last fd reference closes
- **AND** new path lookup for the removed path MUST fail as missing

#### Scenario: rename 后旧 fd 和新路径一致
- **WHEN** a process opens a `/rw` regular file and then restricted-renames that file to a new path in the same writable backend
- **THEN** the pre-rename fd MUST remain valid and continue to reference the same runtime file contents
- **AND** new path lookup for the target path MUST find the renamed object while lookup for the old path fails

### Requirement: 目录 fd 枚举和错误边界稳定
BigOS SHALL provide bounded directory fd enumeration behavior through fd/VFS for supported directory objects. Directory enumeration MUST return bounded entries with at least name and basic type in stable backend order, MUST preserve fd lifecycle rules, and MUST deterministically reject ordinary files, pipes, invalid descriptors, unsupported backends, invalid user buffers, and insufficient caller output capacity. Stable backend order means `/rw` directory slot order and read-only exFAT backend traversal order; it MUST NOT imply lexicographic sorting, POSIX cookies, complete snapshots, full `DIR*` semantics, or a complete `struct dirent` ABI.

#### Scenario: 目录 fd 枚举反映运行期目录项
- **WHEN** a user process creates files or directories under `/rw` and enumerates the parent directory through a valid directory fd
- **THEN** fd/VFS MUST return bounded directory entries that reflect successful current-runtime directory changes in `/rw` directory slot order
- **AND** repeated enumeration of the same unchanged directory state MUST produce the same relative entry order
- **AND** it MUST NOT promise lexicographic sorting, POSIX cookies, full `DIR*` semantics, or a complete `struct dirent` ABI

#### Scenario: exFAT 目录枚举保持后端遍历顺序
- **WHEN** a user process enumerates a supported read-only exFAT directory through a valid directory fd
- **THEN** fd/VFS MUST preserve the exFAT backend traversal order for returned entries
- **AND** it MUST NOT modify boot assets or synthesize a sorted directory view

#### Scenario: 非目录枚举被拒绝
- **WHEN** a user process requests directory enumeration on a regular file fd, pipe fd, invalid fd, closed fd, or unsupported object
- **THEN** fd/VFS MUST return a deterministic error
- **AND** it MUST NOT modify directory state, unrelated fd table entries, or uninitialized user output
