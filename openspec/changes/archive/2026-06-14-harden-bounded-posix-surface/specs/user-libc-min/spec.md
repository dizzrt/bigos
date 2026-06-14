## ADDED Requirements

### Requirement: Minimal signal header and wrappers
The BigOS user libc SHALL expose a bounded `signal.h` surface for installed user programs, including supported signal constants, `sigset_t`, `struct sigaction`, `sigaction`, and `sigprocmask`, backed by the existing signal syscalls.

#### Scenario: User program installs a signal handler
- **WHEN** a static user program includes `signal.h` and calls `sigaction` for a supported signal
- **THEN** the wrapper invokes the BigOS syscall ABI, returns zero on success, and preserves errno translation on failure

#### Scenario: User program changes signal mask
- **WHEN** a static user program calls `sigprocmask` with a supported mask operation
- **THEN** the wrapper updates the process signal mask through the existing syscall and optionally writes the previous mask

### Requirement: Bounded wait wrappers
The BigOS user libc SHALL expose bounded `wait(int *status)` and `waitpid(pid_t pid, int *status, int options)` wrappers while preserving a BigOS-specific compatibility wrapper for callers that need the raw existing wait shape.

#### Scenario: wait waits for any child
- **WHEN** a user program calls `wait` with a status pointer
- **THEN** libc waits for any child, returns the reaped child pid, and writes the raw bounded child status

#### Scenario: waitpid rejects unsupported options
- **WHEN** a user program calls `waitpid` with unsupported options
- **THEN** libc returns `-1`, sets errno to a deterministic error, and does not request a child reap from the kernel

### Requirement: Bounded time and error text interfaces
The BigOS user libc SHALL expose `time`, `strerror`, and `perror` or equivalent bounded C/POSIX-like interfaces without requiring hosted libc, locale, dynamic allocation, or complete stdio semantics.

#### Scenario: time returns kernel seconds
- **WHEN** a user program calls `time` with a non-null output pointer
- **THEN** libc returns the current BigOS wall-clock seconds and stores the same value through the pointer

#### Scenario: perror writes deterministic stderr text
- **WHEN** a user program calls `perror` after a wrapper sets errno
- **THEN** libc writes the optional prefix, separator, stable error text, and newline to fd 2 using the bounded userland output path
