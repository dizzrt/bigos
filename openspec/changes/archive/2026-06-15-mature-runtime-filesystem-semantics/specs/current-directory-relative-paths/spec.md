## ADDED Requirements

### Requirement: Stage 41 文件系统操作共享 cwd 解析
BigOS SHALL route Stage 41 path-taking filesystem operations through the shared bounded cwd-relative path resolution contract. Operations including open, stat, mkdir, unlink, restricted regular-file rename, and directory enumeration setup MUST treat absolute paths as root-relative and relative paths as current-directory-relative with supported `.` and `..` components. This requirement MUST NOT introduce symlink traversal, mount namespaces, `chroot`, stable inode identity, or complete POSIX pathname canonicalization.

#### Scenario: 相对路径写入命中 `/rw` 后端
- **WHEN** a process has cwd `/rw/work` and creates, opens, writes, stats, lists, unlinks, or restricted-renames a relative path under that directory
- **THEN** BigOS MUST resolve the target to the equivalent bounded absolute `/rw` path before backend dispatch
- **AND** the operation MUST observe the same writable backend semantics as the equivalent absolute path

#### Scenario: 相对路径只读写失败保持一致
- **WHEN** a process has cwd under a read-only exFAT path and attempts create, write, unlink, mkdir, or restricted rename through a relative path
- **THEN** BigOS MUST resolve the target through the same bounded cwd contract
- **AND** it MUST return the same deterministic read-only or unsupported error as the equivalent absolute target without mutating filesystem state

### Requirement: cwd 状态不被文件系统失败污染
BigOS SHALL preserve each process current-directory state across failed Stage 41 filesystem operations. Failed open, stat, mkdir, unlink, restricted rename, directory enumeration setup, or backend dispatch MUST NOT modify cwd, even when the failure is caused by path length, unsupported path form, missing path, permission denial, capacity exhaustion, invalid user buffer, or read-only backend mutation attempts.

#### Scenario: 失败路径不改变 cwd
- **WHEN** a process with cwd `/rw/work` performs a failing filesystem operation using a relative path
- **THEN** BigOS MUST leave the process cwd unchanged
- **AND** later relative path operations MUST continue to resolve from the original cwd

#### Scenario: rename 双路径解析失败不改变 cwd
- **WHEN** a process invokes restricted rename with one or both paths relative to cwd and either source or target resolution fails
- **THEN** BigOS MUST return a deterministic error
- **AND** it MUST NOT change cwd, fd table state, source directory state, or target directory state
