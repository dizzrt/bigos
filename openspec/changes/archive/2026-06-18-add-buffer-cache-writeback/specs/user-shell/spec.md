## ADDED Requirements

### Requirement: Shell sync 内建命令

BigOS shell SHALL provide a bounded `sync` builtin that invokes the libc `sync()` wrapper in the shell process. The builtin MUST report success or deterministic errno-based failure through the existing shell output/error path, MUST return to the read-parse-execute loop after completion, and MUST NOT imply complete POSIX shell semantics, background jobs, async flush, full POSIX `sync(2)`, or crash recovery.

#### Scenario: sync 内建成功
- **WHEN** a user enters `sync` in `/bin/sh` and the libc `sync()` wrapper succeeds
- **THEN** shell MUST return to the next prompt/read loop without corrupting shell fd, cwd, or command state
- **AND** the synchronized backend state MUST follow the kernel explicit synchronization contract

#### Scenario: sync 内建失败可见
- **WHEN** a user enters `sync` and libc `sync()` fails with `errno`
- **THEN** shell MUST print a deterministic error message through stdout or stderr
- **AND** shell MUST remain usable for subsequent commands

#### Scenario: sync 不经 fork 执行
- **WHEN** shell recognizes `sync` as a builtin
- **THEN** it MUST execute the synchronization in the shell process
- **AND** it MUST NOT require an external `/bin/sync` tool or complete POSIX command environment
