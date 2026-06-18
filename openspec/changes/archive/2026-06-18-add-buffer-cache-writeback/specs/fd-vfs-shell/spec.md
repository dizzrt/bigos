## ADDED Requirements

### Requirement: VFS 显式同步 writable backend

BigOS SHALL provide a bounded VFS/kernel explicit synchronization entry that flushes dirty state for the active writable backend through the page/buffer cache device-scoped write-back path. The entry MUST first complete any pending persistent metadata commit plan required by the writable backend, MUST then synchronize dirty cache blocks for that backend's backing device, and MUST return deterministic errors without claiming full POSIX `sync(2)` semantics.

#### Scenario: 显式同步 persistent backend 成功
- **WHEN** a user process invokes the explicit synchronization syscall while persistent `/rw` has dirty data or metadata and the call runs from a blockable process context
- **THEN** VFS/bigfs MUST synchronize the required pending metadata and dirty cache blocks through the device-scoped cache path
- **AND** the syscall MUST return success only after the writable backend's required dirty state has been written successfully

#### Scenario: 显式同步 RAM-backed backend 保持运行期边界
- **WHEN** the active writable backend is RAM-backed and a user process invokes explicit synchronization
- **THEN** BigOS MUST flush dirty cache state to the RAM-backed device according to the same cache contract
- **AND** it MUST NOT describe the result as cross-reboot persistence

#### Scenario: 显式同步失败返回 errno
- **WHEN** explicit synchronization encounters metadata commit failure, cache write-back failure, backing-device I/O failure, or a nonblocking-context rejection
- **THEN** BigOS MUST return a deterministic negative errno through the syscall path
- **AND** it MUST preserve dirty or pending state instead of publishing durable success

### Requirement: 显式同步 syscall 边界保持有界

BigOS SHALL expose the explicit synchronization entry through the bounded syscall/VFS surface without changing existing syscall semantics, boot ABI, IDT/syscall vector, user metadata structures, or storage layout. The entry MUST synchronize only BigOS's active writable backend dirty state and MUST NOT imply complete POSIX global filesystem synchronization, mount namespaces, async write-back, or background flush daemons.

#### Scenario: syscall 不改变既有 fd 行为
- **WHEN** explicit synchronization succeeds or fails
- **THEN** existing open file descriptors, offsets, cwd state, and read-only exFAT state MUST remain explainable according to the existing VFS contracts
- **AND** the syscall MUST NOT require callers to provide an fd

#### Scenario: unsupported context does not block
- **WHEN** explicit synchronization is reached from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking context
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT issue blocking storage I/O from that context
