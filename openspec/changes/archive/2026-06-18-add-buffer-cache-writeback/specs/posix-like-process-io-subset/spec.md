## ADDED Requirements

### Requirement: 有界显式同步纳入 POSIX-like I/O 子集

BigOS SHALL include explicit writable-backend synchronization in the bounded POSIX-like process and I/O subset. Simple static C programs and the interactive shell MAY rely on libc `sync()` and shell `sync` to request synchronization of BigOS's active writable backend dirty state through the kernel/VFS/cache path. This subset MUST remain bounded and MUST NOT imply complete POSIX `sync(2)`, `fdatasync`, async I/O, background write-back, mount namespaces, crash recovery, power-loss safety, or complete POSIX filesystem compatibility.

#### Scenario: 简单程序请求显式同步
- **WHEN** a simple static C program writes bounded data under `/rw` and calls libc `sync()`
- **THEN** BigOS MUST route the request through the documented syscall/VFS/cache synchronization path
- **AND** the program MUST observe success or errno-based failure through the libc wrapper

#### Scenario: shell 用户请求显式同步
- **WHEN** a user enters the shell `sync` builtin after writable backend mutations
- **THEN** shell MUST invoke the bounded libc/kernel synchronization path
- **AND** user-visible success or failure MUST remain observable through the shell output/error path

#### Scenario: 文档不扩大 POSIX 承诺
- **WHEN** BigOS documentation, OpenSpec artifacts, validation output, user headers, or shell help describe explicit synchronization
- **THEN** they MUST describe it as a bounded BigOS writable-backend synchronization subset
- **AND** they MUST NOT imply complete POSIX global sync, crash recovery, power-loss safety, async I/O, or broad storage/device support
