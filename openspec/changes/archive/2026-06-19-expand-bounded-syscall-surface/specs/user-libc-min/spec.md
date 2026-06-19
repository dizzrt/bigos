## ADDED Requirements

### Requirement: libc wrappers for bounded syscall surface

BigOS SHALL expose freestanding-safe libc declarations and wrappers for the expanded bounded syscall surface. Wrappers MUST translate negative kernel errno returns into user-visible `errno` results according to existing libc conventions, define only the constants and structures required by the supported subset, and avoid hosted libc, dynamic linking, locale, thread-local storage, or OS services not implemented by BigOS.

#### Scenario: wait headers expose bounded status helpers

- **WHEN** a user program includes the bounded wait header and calls supported `wait` or `waitpid` forms
- **THEN** libc MUST provide declarations, `WNOHANG`, supported status helper macros, and wrapper behavior over the kernel wait syscall contract
- **AND** unsupported options MUST set deterministic `errno` rather than silently succeeding

#### Scenario: fd-control headers expose bounded constants

- **WHEN** a user program includes the bounded fd-control header and queries, sets close-on-exec, or duplicates descriptors with `F_DUPFD`
- **THEN** libc MUST provide only the supported constants and wrappers for `F_GETFD`, `F_SETFD`, `FD_CLOEXEC`, and `F_DUPFD`
- **AND** unsupported commands MUST fail deterministically without requiring complete POSIX `fcntl`

#### Scenario: metadata and access wrappers use bounded structures

- **WHEN** a user program calls supported `stat`/`fstat`/`access`/`truncate` style wrappers
- **THEN** libc MUST use BigOS bounded structures, syscall numbers, and errno translation that match the kernel metadata and VFS contracts
- **AND** the wrappers MUST NOT expose fields or flags that the kernel cannot populate deterministically

### Requirement: libc documentation and headers preserve subset boundary

BigOS SHALL document the expanded libc surface as a bounded freestanding subset. Header comments and user-facing docs MUST distinguish supported wrappers from complete POSIX libc and keep English and Simplified Chinese documentation mirrors synchronized when documentation files are changed.

#### Scenario: headers mark unsupported complete POSIX behavior

- **WHEN** a header introduces POSIX-like names for bounded BigOS wrappers
- **THEN** the header MUST identify the BigOS subset boundary for unsupported options, flags, structures, or semantics
- **AND** it MUST avoid declarations for unsupported complete POSIX features

#### Scenario: documentation mirrors stay synchronized

- **WHEN** implementation changes require repository documentation updates for the expanded libc/syscall surface
- **THEN** docs/en and docs/zh MUST describe the same capability boundaries using matching relative paths
- **AND** documentation MUST NOT imply dynamic linking, complete POSIX libc, SMP, UEFI runtime parity, or broad filesystem compatibility
