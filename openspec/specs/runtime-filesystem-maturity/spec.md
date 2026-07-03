# runtime-filesystem-maturity Specification

## Purpose
TBD - created by archiving change mature-runtime-filesystem-semantics. Update Purpose after archive.
## Requirements
### Requirement: 当前运行期文件系统语义成熟化
BigOS SHALL provide a coherent current-runtime filesystem behavior contract for ordinary static user programs across read-only exFAT boot assets and the RAM-backed `/rw` writable backend. Successful filesystem operations MUST be consistently observable through path lookup, fd I/O, metadata queries, directory enumeration, libc wrappers, shell tools, and validation paths within the same boot session. This capability MUST NOT imply cross-reboot persistence, disk-backed writable partitions, journaling, async I/O, broad storage drivers, broad file-backed `mmap`, or complete POSIX filesystem compatibility.

#### Scenario: 普通程序组合使用运行期文件系统
- **WHEN** a simple static user program creates a directory under `/rw`, creates a file, writes bounded data, seeks to the beginning, reads it back, queries metadata, lists the parent directory, renames the regular file, and unlinks it
- **THEN** BigOS MUST expose each successful operation consistently to subsequent operations in the same boot session
- **AND** read-only exFAT paths MUST remain readable and isolated from `/rw` mutations
- **AND** directory listings for unchanged directories MUST use the stable backend order defined by the fd/VFS directory enumeration contract

#### Scenario: 运行期一致性不等于持久化
- **WHEN** documentation, specs, validation output, user tools, or shell-visible behavior describe successful `/rw` updates
- **THEN** they MUST describe the guarantee as current-runtime behavior over the RAM-backed writable backend
- **AND** they MUST NOT claim reboot persistence, disk partition ownership, journal replay, or modification of the existing MBR/exFAT boot image

### Requirement: 文件系统失败路径保持可解释状态
BigOS SHALL make filesystem failures deterministic and state-preserving for ordinary process-context callers. Failed operations caused by missing paths, existing targets, invalid object types, read-only backends, permission denial, capacity exhaustion, invalid descriptors, illegal user buffers, unsupported seek/enumeration targets, backend I/O errors, allocation failure, or nonblocking-context rejection MUST return stable negative errno values and MUST NOT publish partially initialized objects or corrupt unrelated runtime state.

#### Scenario: 失败操作不发布半成品状态
- **WHEN** a filesystem operation fails before a valid commit point because of permission, capacity, path, object type, user-buffer, allocation, or backend I/O failure
- **THEN** BigOS MUST leave existing file contents, directory entries, inode metadata, fd table entries, cwd state, open file offsets, and read-only exFAT state explainable from before the failed operation
- **AND** it MUST NOT require callers to inspect internal backend enum names or debug strings

#### Scenario: 不可阻塞上下文拒绝副作用
- **WHEN** filesystem code would allocate, block, issue synchronous block I/O, flush dirty cache state, or mutate directory entries from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT publish fd entries, dirty cache blocks, directory entries, inode metadata, or partial user output from that context

### Requirement: 持久存储准备边界
BigOS SHALL keep the runtime filesystem maturity work compatible with the persistent clean-sync /rw storage milestone while preserving the distinction between current-runtime consistency and cross-reboot persistence. RAM-backed `/rw` semantics MUST remain current-session-only. Persistent `/rw` semantics MAY reuse the mature runtime contracts for create, write, read, metadata, directory changes, fd references, errno behavior, and directory enumeration, but MUST define additional disk layout, mount-existing, explicit format, clean sync, and clean-reboot validation requirements separately. This capability MUST NOT imply journaling, crash recovery, async I/O, broad storage drivers, broad file-backed `mmap`, dynamic linking, or complete POSIX filesystem compatibility.

#### Scenario: runtime filesystem maturity RAM-backed 行为仍不改变磁盘布局
- **WHEN** runtime filesystem maturity behavior is implemented and validated without enabling the persistent writable backend
- **THEN** the existing x86_64 Legacy BIOS/MBR/exFAT boot image layout, read-only boot assets, exFAT discovery path, and kernel/user packaging path MUST remain unchanged
- **AND** `/rw` MUST still initialize as a RAM-backed current-session writable backend unless an accepted persistent storage configuration explicitly selects a persistent backend

#### Scenario: persistent clean-sync /rw storage 持久化复用成熟运行期语义
- **WHEN** the persistent writable filesystem backend is selected and a compatible persistent volume is mounted
- **THEN** it MUST reuse the mature runtime semantics for create, open, write, read, lseek, fsync, mkdir, unlink, restricted regular-file rename, metadata queries, directory enumeration, fd references, and deterministic errno behavior
- **AND** it MUST add the separate persistent volume recognition, explicit format, clean sync, and clean-reboot visibility requirements defined by the persistent writable filesystem capability

#### Scenario: 运行期一致性与跨重启持久性仍被区分
- **WHEN** documentation, specs, validation output, user tools, or shell-visible behavior describe `/rw` behavior
- **THEN** they MUST state whether the active backend is RAM-backed current-runtime storage or persistent clean-sync storage
- **AND** they MUST NOT claim journaling, crash recovery, or persistence for unsynchronized dirty data

### Requirement: 运行期目录树状态跨接口一致
BigOS SHALL expose successful `/rw` directory tree mutations consistently across path lookup, fd I/O, metadata queries, bounded directory enumeration, cwd-relative resolution, libc wrappers, shell commands, and packaged path tools within the same boot session. This requirement applies to the active writable backend's current-runtime state and MUST NOT imply cross-reboot persistence unless the persistent clean-sync backend separately commits the state.

#### Scenario: shell 和用户程序观察同一目录树
- **WHEN** a user program creates `/rw/tree/sub`, creates multiple files under it, and a shell command or packaged path tool later lists or stats the same paths in the same boot session
- **THEN** BigOS MUST expose the same live directory entries, object types, and bounded metadata through both programmatic and shell-visible paths
- **AND** relative paths from cwd inside the directory tree MUST resolve according to the existing bounded path resolution contract

#### Scenario: 删除后所有运行期接口收敛
- **WHEN** a file is unlinked or an empty directory is removed successfully under `/rw`
- **THEN** subsequent open, stat, directory enumeration, and shell/path-tool observations of the parent directory MUST agree that the removed path is no longer present
- **AND** unrelated sibling entries MUST remain observable

### Requirement: 目录树失败路径保持运行期状态可解释
BigOS SHALL keep runtime filesystem state explainable when directory tree operations fail. Failed creation, deletion, lookup, enumeration, or metadata operations caused by missing paths, existing targets, invalid object types, read-only backends, permission denial, capacity exhaustion, invalid descriptors, illegal user buffers, backend I/O errors, allocation failure, or nonblocking-context rejection MUST return stable negative errno values and MUST NOT publish partial objects or corrupt unrelated state.

#### Scenario: 非法用户缓冲不改变目录树
- **WHEN** a syscall that copies path, metadata, or directory entries to or from user memory fails user-buffer validation
- **THEN** BigOS MUST return a deterministic fault or invalid-argument error
- **AND** it MUST NOT create, remove, rename, or partially enumerate directory tree entries as a side effect

#### Scenario: 不可阻塞上下文拒绝目录树副作用
- **WHEN** directory tree code would allocate, block, issue synchronous block I/O, flush dirty cache state, or mutate directory entries from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT publish fd entries, dirty cache blocks, directory entries, inode metadata, cwd changes, or partial user output from that context

### Requirement: runtime filesystem descriptions distinguish journaled writes from recovery
BigOS SHALL describe active `/rw` filesystem behavior according to the selected backend and implemented durability stage. RAM-backed `/rw` remains current-runtime-only; persistent `/rw` may provide journaled write-ahead commit ordering when mounted with the journal-capable format; mount-time replay/discard and crash recovery remain separate future capabilities until implemented.

#### Scenario: user-visible diagnostics name journaled persistent backend accurately
- **WHEN** diagnostics, validation output, docs, or shell-visible metadata describe the active persistent `/rw` backend after M15.1
- **THEN** they MUST distinguish journaled write-ahead commit ordering from mount-time recovery
- **AND** they MUST NOT claim that non-clean shutdown recovery, replay, discard, or repair is available

#### Scenario: RAM-backed runtime behavior remains current-session-only
- **WHEN** `/rw` is using the RAM-backed current-session backend
- **THEN** BigOS MUST keep existing runtime filesystem behavior and failure semantics
- **AND** documentation or validation MUST NOT imply that journaled persistent ordering applies to RAM-backed state

### Requirement: filesystem validation records journaling stage boundaries
BigOS SHALL record journaling validation results separately from runtime filesystem maturity, persistent clean-sync, and future recovery validation. Passing M15.1 journaling validation MUST mean the journal-first write path and deterministic failure behavior were exercised, not that crash recovery has passed.

#### Scenario: journaling validation passes without recovery claim
- **WHEN** default-off journaling validation completes successfully
- **THEN** the result MUST identify the checked journal-first ordering and clean validation boundary
- **AND** it MUST record mount-time replay/discard recovery as not covered by this validation

#### Scenario: recovery-dependent check is unavailable
- **WHEN** a validation scenario would require rebooting after an unclean shutdown and replaying or discarding a journal
- **THEN** M15.1 validation MUST mark that scenario as out of scope, skipped, or blocked
- **AND** it MUST NOT report the scenario as passed by ordinary clean reboot readback
