## MODIFIED Requirements

### Requirement: 持久存储准备边界
BigOS SHALL keep the Stage 41 runtime filesystem maturity work compatible with the Stage 44 persistent writable storage milestone while preserving the distinction between current-runtime consistency and cross-reboot persistence. RAM-backed `/rw` semantics MUST remain current-session-only. Persistent `/rw` semantics MAY reuse the mature runtime contracts for create, write, read, metadata, directory changes, fd references, errno behavior, and directory enumeration, but MUST define additional disk layout, mount-existing, explicit format, clean sync, and clean-reboot validation requirements separately. This capability MUST NOT imply journaling, crash recovery, async I/O, broad storage drivers, broad file-backed `mmap`, dynamic linking, or complete POSIX filesystem compatibility.

#### Scenario: Stage 41 RAM-backed 行为仍不改变磁盘布局
- **WHEN** runtime filesystem maturity behavior is implemented and validated without enabling the persistent writable backend
- **THEN** the existing x86_64 Legacy BIOS/MBR/exFAT boot image layout, read-only boot assets, exFAT discovery path, and kernel/user packaging path MUST remain unchanged
- **AND** `/rw` MUST still initialize as a RAM-backed current-session writable backend unless an accepted persistent storage configuration explicitly selects a persistent backend

#### Scenario: Stage 44 持久化复用成熟运行期语义
- **WHEN** the persistent writable filesystem backend is selected and a compatible persistent volume is mounted
- **THEN** it MUST reuse the mature runtime semantics for create, open, write, read, lseek, fsync, mkdir, unlink, restricted regular-file rename, metadata queries, directory enumeration, fd references, and deterministic errno behavior
- **AND** it MUST add the separate persistent volume recognition, explicit format, clean sync, and clean-reboot visibility requirements defined by the persistent writable filesystem capability

#### Scenario: 运行期一致性与跨重启持久性仍被区分
- **WHEN** documentation, specs, validation output, user tools, or shell-visible behavior describe `/rw` behavior
- **THEN** they MUST state whether the active backend is RAM-backed current-runtime storage or persistent clean-sync storage
- **AND** they MUST NOT claim journaling, crash recovery, or persistence for unsynchronized dirty data
