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

BigOS SHALL expose bounded fd-control primitives sufficient for small user programs to query and set close-on-exec state on process-local descriptors, duplicate descriptors with `F_DUPFD`, and query/set the bounded `O_NONBLOCK` status flag on the open file description via `F_GETFL`/`F_SETFL`. The fd-control surface MUST preserve fd table ownership, lowest-available descriptor allocation, open file object reference semantics, and deterministic bad-fd behavior. `F_GETFL` MUST report the access mode together with the `O_NONBLOCK` bit; `F_SETFL` MUST set the open file description's nonblocking flag from the `O_NONBLOCK` bit and MUST ignore all other bits in the argument (access-mode bits, creation bits, and any unimplemented status bits) without error, returning success. It MUST NOT alter the access mode or close-on-exec state. It MUST NOT implement complete POSIX `fcntl`, file locking, signal-driven/async I/O (`O_ASYNC`), `F_DUPFD_CLOEXEC`, or descriptor passing.

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

#### Scenario: F_GETFL 返回访问模式与非阻塞位

- **WHEN** a user process calls `F_GETFL` on a valid open descriptor
- **THEN** BigOS MUST return the access mode bits synthesized from the open file's readable/writable state ORed with `O_NONBLOCK` when the open file description's nonblocking flag is set
- **AND** the query MUST leave the descriptor, open file offset, open file reference, and close-on-exec state unchanged

#### Scenario: F_SETFL 切换非阻塞位

- **WHEN** a user process calls `F_SETFL` on a valid open descriptor with or without the `O_NONBLOCK` bit in the argument
- **THEN** BigOS MUST set the open file description's nonblocking flag to match the `O_NONBLOCK` bit and return success
- **AND** it MUST NOT change the access mode, close-on-exec state, or the open file offset

#### Scenario: F_SETFL 忽略访问模式与不支持位

- **WHEN** a user process calls `F_SETFL` with an argument that also carries access-mode bits, creation bits, or unimplemented status bits (such as the common idiom passing the result of `F_GETFL` ORed with `O_NONBLOCK`)
- **THEN** BigOS MUST apply only the `O_NONBLOCK` bit, ignore every other bit without error, and return success
- **AND** it MUST NOT fail with `-EINVAL` for the carried access-mode bits, MUST NOT change the access mode, and MUST NOT mutate close-on-exec state

#### Scenario: fd 控制拒绝 bad fd

- **WHEN** a user process applies fd-control operations to a closed, out-of-range, or otherwise invalid descriptor
- **THEN** BigOS MUST fail with a deterministic bad-fd errno
- **AND** it MUST NOT access freed file state or allocate a replacement descriptor

### Requirement: 有界文件与路径 primitive

BigOS SHALL provide bounded file and path primitives for access checks, metadata queries, truncation, and explicit timestamp updates through the existing VFS and backend status model. Path-taking operations MUST share the existing bounded path copy and cwd-relative resolution rules. They MUST preserve read-only backend rejection, `/rw` backend permission/capacity failures, deterministic errno mapping, and blocking-context guards.

#### Scenario: 文件 primitive syscall 追加而不改号

- **WHEN** BigOS adds bounded file or timestamp syscalls
- **THEN** BigOS MUST append new syscall numbers after the existing syscall surface or use documented unused entries
- **AND** all existing syscall numbers, argument register order, `int 0x80` return behavior, syscall gate privilege, exception/IRQ EOI rules, boot layout, page-table layout, CR3 switching, and disk image layout MUST remain unchanged

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

#### Scenario: utimens 更新时间戳

- **WHEN** a user process invokes the supported timestamp syscall with a valid path, valid second-resolution timestamp arguments, supported flags, and sufficient permission
- **THEN** BigOS MUST update the target object's atime and mtime according to the request
- **AND** it MUST update ctime to the current bounded timestamp value

#### Scenario: utimens 失败不修改状态

- **WHEN** the timestamp syscall receives an invalid path pointer, invalid timestamp pointer, unsupported flags, missing target, read-only backend target, unsupported object, or insufficient permission
- **THEN** BigOS MUST fail with deterministic negative errno
- **AND** it MUST NOT mutate file timestamps, file data, directory entries, fd table state, or process identity

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

### Requirement: 有界默认终端模式 syscall

BigOS SHALL extend the bounded syscall surface with append-only default terminal mode operations that let simple static user programs query and set the single default terminal's canonical/raw input mode. The operations MUST preserve the existing `int 0x80` vector, register ABI, return convention, syscall no-EOI rule, and deterministic negative errno behavior.

#### Scenario: 查询 terminal mode syscall

- **WHEN** a user process invokes the supported terminal-mode query syscall with a valid user output buffer or equivalent register-return contract
- **THEN** BigOS MUST return the current default terminal mode deterministically
- **AND** the syscall MUST NOT mutate TTY input state, foreground process group state, fd state, or console render state

#### Scenario: 设置 terminal mode syscall

- **WHEN** a permitted foreground/session process invokes the supported terminal-mode set syscall with a valid canonical or raw mode request
- **THEN** BigOS MUST update the default terminal mode and return success
- **AND** subsequent default terminal reads MUST observe the requested mode

#### Scenario: 非法 mode 或非法用户缓冲失败

- **WHEN** a terminal-mode syscall receives an invalid user pointer, unsupported mode value, unknown flag, invalid structure size, or request from a disallowed process
- **THEN** BigOS MUST return deterministic negative errno or follow the documented user fault path
- **AND** it MUST NOT partially update terminal mode or corrupt TTY buffers

#### Scenario: syscall ABI 追加而不改号

- **WHEN** terminal-mode syscalls are added
- **THEN** BigOS MUST append new syscall numbers or otherwise extend only documented unused entries
- **AND** existing syscall numbers, register argument order, `VECTOR_SYSCALL = 0x80`, DPL/syscall gate behavior, and syscall EOI semantics MUST remain unchanged

### Requirement: 有界阻塞式 sleep syscall surface
BigOS SHALL extend the bounded syscall surface with an append-only blocking sleep operation that accepts a millisecond duration and returns success only after the current user process has slept until the coarse monotonic tick deadline expires. The operation MUST preserve the existing `int 0x80` ABI, deterministic negative errno convention, syscall no-EOI rule, and current syscall numbering for all previously defined syscalls.

#### Scenario: sleep syscall 追加到现有 surface
- **WHEN** the blocking sleep syscall is introduced
- **THEN** BigOS MUST add the syscall through an append-only number such as `SYS_SLEEP_MS`
- **AND** existing syscall numbers, register argument order, and return-value conventions MUST remain stable

#### Scenario: syscall 参数使用毫秒单位
- **WHEN** a user process calls the blocking sleep syscall with a supported millisecond duration in `rdi`
- **THEN** BigOS MUST interpret the value as milliseconds rather than seconds, ticks, nanoseconds, or a user pointer
- **AND** the syscall MUST return 0 on normal timeout completion

#### Scenario: syscall 错误不泄漏 scheduler 私有值
- **WHEN** the scheduler sleep primitive reports timeout completion, forbidden blocking, or another internal wait result
- **THEN** the syscall layer MUST translate the result into the documented user-visible success or negative errno value
- **AND** it MUST NOT expose scheduler-private constants such as `WAIT_TIMEOUT` or `WAIT_BLOCK_FORBIDDEN` directly to user mode
