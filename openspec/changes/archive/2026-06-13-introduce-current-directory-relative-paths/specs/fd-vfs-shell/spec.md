## MODIFIED Requirements

### Requirement: 只读绝对路径 open
BigOS SHALL support opening read-only regular files by absolute path or by relative path resolved from the current process cwd through the VFS shell. The operation MUST accept only bounded, NUL-terminated paths and read-only flags for the read-only backend, MUST apply the same path resolution contract as other path-taking VFS operations including POSIX-style `.`/`..` components, and MUST reject unsupported path forms or write-capable flags deterministically.

#### Scenario: open 找到普通文件
- **WHEN** a caller opens an existing absolute path such as `/boot/fs_smoke.txt` with read-only flags
- **THEN** VFS MUST resolve the path through the exFAT backend and create an open file object with offset zero and read operations bound to that file

#### Scenario: open 从 cwd 解析相对路径
- **WHEN** 当前进程 cwd 指向一个支持的目录且调用方以 read-only flags 打开相对路径
- **THEN** VFS MUST 先按 cwd 合成有界目标路径，再通过对应后端创建 open file object
- **AND** 成功打开的 fd MUST 与同一目标绝对路径打开时具有相同读语义

#### Scenario: open 解析 dot-dot 组件
- **WHEN** 当前进程 cwd 为 `/rw/work/sub` 且调用方以 read-only flags 打开 `../note.txt`
- **THEN** VFS MUST resolve the target as `/rw/work/note.txt`
- **AND** the resulting open file behavior MUST match opening that absolute target directly

#### Scenario: open 拒绝不支持的请求
- **WHEN** a caller opens an overlong path, non-NUL-terminated path, non-regular file, missing path, unsupported relative path form, or a path with write/create/truncate flags on a read-only backend
- **THEN** VFS MUST fail with a deterministic error and MUST NOT allocate a published file descriptor or mutate filesystem state

## ADDED Requirements

### Requirement: fd/VFS 路径入口共享 cwd 解析
BigOS SHALL route path-taking fd/VFS operations through a shared bounded path resolution helper that understands both absolute paths and relative paths rooted at the current process cwd, including POSIX-style `.`/`..` component handling within the supported BigOS directory tree. This shared helper MUST be used consistently for open, writable runtime file operations, directory operations, and path-based dispatch where applicable. It MUST NOT introduce mount namespaces, `chroot`, symlink traversal, async I/O, or complete POSIX VFS semantics.

#### Scenario: 可写后端相对路径操作
- **WHEN** 当前进程 cwd 位于 `/rw` 下且进程创建、打开、写入、同步、枚举或删除相对路径
- **THEN** fd/VFS MUST route the operation to the RAM-backed writable backend for the cwd-resolved target
- **AND** backend permission, capacity, reference lifecycle, and deterministic errno behavior MUST remain unchanged

#### Scenario: 只读后端相对写失败
- **WHEN** 当前进程 cwd 位于只读 exFAT 路径下且进程以相对路径请求写入、创建或删除
- **THEN** fd/VFS MUST return the same deterministic read-only or unsupported error as the equivalent absolute target
- **AND** MUST NOT modify filesystem state or publish a partially initialized fd

#### Scenario: 不可阻塞上下文拒绝路径解析副作用
- **WHEN** cwd-based path operation would require allocation, backend lookup, blocking disk I/O, or wait operations from IRQ, scheduler critical section, or preemption-disabled path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** MUST NOT perform blocking I/O or unbounded allocation from that context
