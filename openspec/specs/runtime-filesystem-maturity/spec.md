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
BigOS SHALL keep the Stage 41 runtime filesystem maturity work compatible with a future persistent writable storage milestone without implementing that milestone. Runtime filesystem semantics MUST avoid depending on RAM-only shortcuts that would make later mount-existing, disk layout protection, clean sync, metadata consistency, and reboot-cycle validation impossible to specify.

#### Scenario: Stage 41 不改变磁盘布局
- **WHEN** Stage 41 filesystem maturity changes are implemented and validated
- **THEN** the existing x86_64 Legacy BIOS/MBR/exFAT boot image layout, read-only boot assets, exFAT discovery path, and kernel/user packaging path MUST remain unchanged
- **AND** `/rw` MUST still initialize as a RAM-backed current-session writable backend unless a later accepted change explicitly introduces persistent storage

#### Scenario: 后续持久化可以复用成熟语义
- **WHEN** a later milestone chooses persistent writable storage
- **THEN** it MAY reuse the Stage 41 current-runtime semantics for create, write, read, metadata, directory changes, fd references, and errno behavior
- **AND** it MUST define additional disk layout, mount-existing, recovery, and reboot persistence requirements separately

