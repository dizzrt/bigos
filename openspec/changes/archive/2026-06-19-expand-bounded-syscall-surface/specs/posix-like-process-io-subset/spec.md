## ADDED Requirements

### Requirement: 扩展 bounded POSIX-like syscall 消费面

BigOS SHALL expand its bounded POSIX-like process and I/O subset with small-program-friendly wrappers for wait variants including `WNOHANG`, fd close-on-exec control, `F_DUPFD`, access/metadata/truncation operations, and process information queries. The subset MUST remain explicitly bounded and MUST NOT imply complete POSIX process control, job control, file locking, nonblocking I/O, async I/O, mount namespaces, dynamic linking, or complete libc compatibility.

#### Scenario: 小型静态程序使用标准形态 wrapper

- **WHEN** a freestanding static user program uses the supported bounded wrappers for waiting, fd control, metadata queries, truncation, and process information
- **THEN** BigOS MUST provide declarations and runtime behavior sufficient for that program to compile and observe deterministic success or errno results
- **AND** wrappers whose names resemble POSIX MUST document the BigOS subset limits

#### Scenario: unsupported POSIX 行为确定性失败

- **WHEN** a user program requests unsupported wait options other than the bounded `WNOHANG` subset, complete `fcntl` operations outside `F_GETFD`/`F_SETFD`/`F_DUPFD`, file locking, nonblocking mode, broad permissions, symlink traversal, mount behavior, or complete job-control behavior
- **THEN** BigOS MUST fail with deterministic unsupported or invalid-argument errno
- **AND** it MUST NOT partially emulate behavior that would imply a broader POSIX contract

### Requirement: shell and userland preserve existing behavior

BigOS SHALL keep the default init and `/bin/sh` path compatible with the expanded syscall surface. Existing shell commands, redirection, single pipe behavior, foreground process group handling, and bounded `/bin/*` programs MUST continue to work while optionally consuming the new wrappers where they reduce BigOS-specific raw calls.

#### Scenario: shell wait path remains deterministic

- **WHEN** `/bin/sh` runs a foreground external command or single pipe after the expanded wait wrappers are available
- **THEN** the shell MUST still wait for the intended child processes and report deterministic command status
- **AND** unsupported background job or complete job-control syntax MUST remain outside the supported subset

#### Scenario: existing binaries keep syscall compatibility

- **WHEN** existing packaged user programs use previously supported syscall wrappers
- **THEN** BigOS MUST preserve their successful behavior and errno mapping
- **AND** new wrapper additions MUST NOT require dynamic linking, shared libraries, hosted libc behavior, or new runtime startup assumptions
