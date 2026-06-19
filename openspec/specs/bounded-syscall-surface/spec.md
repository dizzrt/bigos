## Purpose

定义 BigOS 扩展有界 syscall surface 的用户可见契约：在既有 `int 0x80` ABI、
进程生命周期、fd/VFS、最小 libc 和可复现验证边界内，向简单静态用户程序暴露
wait 变体、fd control、文件/路径 primitive 与进程信息查询。该能力保持有界，
不声明完整 POSIX `waitpid`、`fcntl`、job control、文件锁、async I/O、动态链接、
完整 libc 或完整 POSIX 进程/文件系统语义。

## Requirements

### Requirement: 有界 wait 变体消费面

BigOS SHALL provide bounded user-visible wait variants that allow a process to wait for any child or one specific child while preserving the existing safe zombie/reap lifecycle. The wait variants MUST support the BigOS bounded `WNOHANG` subset for nonblocking reap checks and MUST return deterministic errno values for unsupported options, invalid child selectors, invalid user status pointers, interrupted waits, and no-child cases. They MUST NOT claim complete POSIX `waitpid`, stopped/continued state, process-group waits, resource usage reporting, or job-control semantics.

#### Scenario: wait 任意子进程

- **WHEN** a parent process calls the bounded `wait` wrapper with a valid optional status pointer and at least one exited child exists
- **THEN** BigOS MUST reap one eligible child, copy the bounded exit status when requested, and return the reaped child pid
- **AND** it MUST close or release process-owned resources only through the existing safe reaper boundary

#### Scenario: waitpid 等待指定子进程

- **WHEN** a parent process calls the bounded `waitpid` wrapper with a positive child pid, options equal to zero, and that child exits
- **THEN** BigOS MUST reap that specific child, copy the bounded status when requested, and return that child pid
- **AND** it MUST NOT reap unrelated siblings while satisfying the specific wait

#### Scenario: waitpid WNOHANG 未命中立即返回

- **WHEN** a parent process calls the bounded `waitpid` wrapper with `WNOHANG`, a supported selector, and no matching exited child is currently reapable
- **THEN** BigOS MUST return 0 without blocking
- **AND** it MUST NOT reap unrelated children, subscribe to asynchronous wait notification, or mutate the state of running children

#### Scenario: waitpid WNOHANG 命中已退出子进程

- **WHEN** a parent process calls the bounded `waitpid` wrapper with `WNOHANG`, a supported selector, and a matching exited child is already reapable
- **THEN** BigOS MUST reap the matching child, copy the bounded status when requested, and return that child pid
- **AND** the result MUST follow the same zombie/reap and resource release rules as the blocking wait path

#### Scenario: waitpid 拒绝 unsupported options

- **WHEN** a process calls the bounded `waitpid` wrapper with unsupported options, process-group selectors, or selectors outside the BigOS bounded contract
- **THEN** BigOS MUST fail with a deterministic errno
- **AND** it MUST NOT reap any child or mutate parent-visible wait state

### Requirement: 有界 fd 控制 primitive

BigOS SHALL expose bounded fd-control primitives sufficient for small user programs to query and set close-on-exec state on process-local descriptors and duplicate descriptors with `F_DUPFD`. The fd-control surface MUST preserve fd table ownership, lowest-available descriptor allocation, open file object reference semantics, and deterministic bad-fd behavior. It MUST NOT implement complete POSIX `fcntl`, file locking, nonblocking I/O, async I/O, `F_DUPFD_CLOEXEC`, or descriptor passing.

#### Scenario: 设置 close-on-exec

- **WHEN** a user process requests close-on-exec on a valid open descriptor through the bounded fd-control interface
- **THEN** BigOS MUST mark that fd table entry so a successful `execve` commit closes it before entering the new user image
- **AND** the operation MUST NOT close the descriptor immediately

#### Scenario: 查询 close-on-exec

- **WHEN** a user process queries close-on-exec state for a valid open descriptor
- **THEN** BigOS MUST return whether the fd table entry is marked close-on-exec
- **AND** it MUST leave the descriptor, open file offset, and open file reference unchanged

#### Scenario: F_DUPFD 分配最低可用 descriptor

- **WHEN** a user process requests `F_DUPFD` on a valid descriptor with a supported minimum descriptor value
- **THEN** BigOS MUST allocate and return the lowest available descriptor greater than or equal to that minimum
- **AND** the new descriptor MUST reference the same open file object, share the same open file offset semantics as existing duplication paths, and start with close-on-exec cleared

#### Scenario: F_DUPFD 拒绝非法起点或容量不足

- **WHEN** a user process requests `F_DUPFD` with an invalid minimum descriptor, an invalid source descriptor, or no available descriptor within the bounded fd table limits
- **THEN** BigOS MUST fail with a deterministic errno
- **AND** it MUST NOT allocate a partial descriptor, leak an open file reference, or mutate the source descriptor flags

#### Scenario: fd 控制拒绝 bad fd

- **WHEN** a user process applies fd-control operations to a closed, out-of-range, or otherwise invalid descriptor
- **THEN** BigOS MUST fail with a deterministic bad-fd errno
- **AND** it MUST NOT access freed file state or allocate a replacement descriptor

### Requirement: 有界文件与路径 primitive

BigOS SHALL provide bounded file and path primitives for access checks, metadata queries, and truncation through the existing VFS and backend status model. Path-taking operations MUST share the existing bounded path copy and cwd-relative resolution rules. They MUST preserve read-only backend rejection, `/rw` backend permission/capacity failures, deterministic errno mapping, and blocking-context guards.

#### Scenario: access 检查路径可见性

- **WHEN** a user process requests a bounded access check for an existing path and supported access mode bits
- **THEN** BigOS MUST resolve the path through the same VFS path rules used by open/stat-like operations and return success only when the requested bounded mode is permitted by the target backend
- **AND** unsupported mode bits MUST fail deterministically without mutating filesystem state

#### Scenario: stat 和 fstat 返回 metadata

- **WHEN** a user process requests metadata for a valid path or open descriptor with a valid output buffer
- **THEN** BigOS MUST copy bounded file metadata to user memory using the existing metadata contract
- **AND** it MUST reject invalid user buffers, missing paths, bad descriptors, directories-as-files, and unsupported backend states with deterministic errno values

#### Scenario: truncate 类操作保持 backend 边界

- **WHEN** a user process truncates a writable `/rw` regular file by path or descriptor within supported size limits
- **THEN** BigOS MUST update file size through the VFS writable backend and page/buffer-cache synchronization rules
- **AND** read-only paths, directories, invalid lengths, insufficient capacity, and permission failures MUST return deterministic errno values without partial publication

### Requirement: 有界进程信息 primitive

BigOS SHALL expose bounded process information primitives needed by small static programs, including current pid, parent pid, process group, session, and identity queries already represented by the process model. These primitives MUST be read-only unless explicitly documented as a bounded control operation, and they MUST return deterministic errors for missing target processes or unsupported selectors.

#### Scenario: 查询当前进程身份

- **WHEN** a user process queries its pid, parent pid, uid, gid, pgid, or sid through the bounded libc wrappers
- **THEN** BigOS MUST return values derived from the current process object and its existing ownership/session state
- **AND** the query MUST NOT allocate, block on storage I/O, or mutate process lifecycle state

#### Scenario: 查询不存在进程失败

- **WHEN** a user process queries process-group or session information for a target pid that does not exist or is no longer observable
- **THEN** BigOS MUST fail with a deterministic errno
- **AND** it MUST NOT resurrect, retain, or dereference a reaped process object

### Requirement: syscall surface 验证可复现

BigOS SHALL provide reproducible validation for the expanded bounded syscall surface. Validation MUST cover source-level ABI contracts, user pointer validation, errno mapping, fd close-on-exec lifecycle, wait variants, file/path primitive behavior, and basic process information queries. Runtime validation MUST use the default x86_64 Legacy BIOS path when emulator dependencies are available and MUST record unavailable toolchain or emulator dependencies as skipped rather than passed.

#### Scenario: source-level checks 覆盖新增 syscall contract

- **WHEN** this change is implemented
- **THEN** source-level checks MUST verify syscall numbering stability, wrapper declarations, errno mapping, wait variant dispatch, fd-control behavior, and metadata/access/truncate primitive routing
- **AND** those checks MUST distinguish current-change failures from pre-existing diagnostics

#### Scenario: runtime smoke 覆盖用户可见行为

- **WHEN** runtime validation is enabled in an environment with the required cross toolchain, disk image, and QEMU headless support
- **THEN** BigOS MUST run a bounded userland scenario that observes wait variants, close-on-exec behavior, metadata/access/truncate operations, and process information queries
- **AND** unavailable QEMU, Bochs, ROM/display, raw-image, serial oracle, or timeout dependencies MUST be recorded as skipped validation with residual risk
