## ADDED Requirements

### Requirement: libc maturity consumes bounded process and I/O subset

BigOS portable libc subset SHALL expose POSIX-like process and I/O wrappers only as a userland consumption layer over the existing bounded process, fd, VFS, directory, cwd, pipe/dup, wait, time, identity, signal, and error-reporting capabilities. The libc layer MUST preserve kernel errno translation and documented failure sentinels. It MUST NOT claim complete POSIX process behavior, complete filesystem semantics, job control, terminal process groups, full shell compatibility, permissions model, dynamic linking, async I/O, SMP, or complete POSIX libc.

#### Scenario: portable program uses documented wrappers

- **WHEN** 一个简单静态 C 程序 uses portable libc subset wrappers for file, directory, cwd, wait, pipe/dup, time, identity, or error reporting
- **THEN** the wrappers MUST consume the existing bounded kernel/user ABI behavior
- **AND** the program MUST NOT need raw syscall primitives, private kernel headers, or a complete POSIX runtime

#### Scenario: unsupported POSIX behavior remains outside libc

- **WHEN** a requested process, filesystem, terminal, shell, or permission behavior is outside the documented BigOS bounded subset
- **THEN** libc MUST report failure or leave the interface unsupported according to the documented boundary
- **AND** MUST NOT emulate full POSIX behavior in conflict with kernel/VFS/process semantics

### Requirement: portable wrapper composition is observable

BigOS SHALL validate that portable libc subset wrappers compose with the bounded process and I/O subset through representative user-visible behavior. Validation MUST cover at least one combination of process or shell execution with stdout/stderr, errno/error text, file or directory operation, and failure reporting. Environment-dependent checks MAY be skipped only with explicit records of missing emulator, toolchain, image configuration, display/ROM dependency, or timeout oracle.

#### Scenario: validation observes wrapper composition

- **WHEN** portable wrapper composition validation runs in a configured emulator environment
- **THEN** it MUST observe representative output, error reporting, and file or directory wrapper behavior through the current shell, process, fd, VFS, console, serial, or exit-status path
- **AND** the result MUST be decidable from deterministic low-level output or status

#### Scenario: skipped runtime composition check is recorded

- **WHEN** runtime validation cannot run because local emulator, cross toolchain, image configuration, display/ROM dependency, or timeout oracle is unavailable
- **THEN** validation notes MUST record the missing condition and substitute checks
- **AND** MUST state the remaining risk for process/I/O composition coverage
