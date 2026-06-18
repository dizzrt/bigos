## ADDED Requirements

### Requirement: libc 提供有界 sync wrapper

BigOS 用户态 libc SHALL provide a bounded `sync()` wrapper for the explicit writable-backend synchronization syscall. The wrapper MUST issue the documented syscall using the existing `int 0x80` ABI, MUST return `0` on success, MUST translate negative kernel errno returns into positive user `errno` plus `-1`, and MUST NOT claim complete POSIX `sync(2)`, async write-back, background flush, mount namespace, or full filesystem synchronization semantics.

#### Scenario: sync wrapper 成功
- **WHEN** a user program calls `sync()` and the kernel explicit synchronization syscall succeeds
- **THEN** libc MUST return `0`
- **AND** it MUST NOT modify `errno` because the call succeeded

#### Scenario: sync wrapper 失败设置 errno
- **WHEN** the kernel explicit synchronization syscall returns a negative errno because write-back, metadata commit, I/O, or context validation failed
- **THEN** libc MUST set `errno` to the corresponding positive value
- **AND** it MUST return `-1`

#### Scenario: 头文件声明保持有界
- **WHEN** a simple static C program includes the BigOS userland header that exposes file/sync wrappers
- **THEN** the program MUST see a declaration for `sync()`
- **AND** the header MUST NOT imply hosted libc, complete POSIX libc, full `sync(2)`, `fdatasync`, or async I/O support
