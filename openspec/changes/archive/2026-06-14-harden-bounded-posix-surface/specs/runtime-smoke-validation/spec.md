## ADDED Requirements

### Requirement: bounded POSIX-like surface smoke coverage
BigOS SHALL provide default-off runtime smoke or equivalent source-contract validation for the bounded POSIX-like surface hardening work.

#### Scenario: Signal surface validation
- **WHEN** the bounded POSIX-like surface validation path exercises signal behavior
- **THEN** it covers installing a handler, delivering a signal, returning through the bounded sigreturn path, and preserving the default termination behavior for an unhandled signal

#### Scenario: Wait and status validation
- **WHEN** the bounded POSIX-like surface validation path exercises process waiting
- **THEN** it covers waiting for any child, waiting for a specific child, status writeback, and deterministic failure for unsupported wait options

#### Scenario: Error text validation
- **WHEN** the bounded POSIX-like surface validation path exercises libc error reporting
- **THEN** it covers errno translation, stable strerror text for known errors, fallback text for unknown errors, and perror output to stderr

#### Scenario: Shell composition validation
- **WHEN** the bounded POSIX-like surface validation path exercises shell behavior
- **THEN** it covers successful commands, command-not-found, unsupported syntax, failed redirection recovery, single-stage pipe EOF, and bounded status reporting

### Requirement: bounded POSIX-like surface source-contract validation
BigOS SHALL keep user/kernel mirror contracts synchronized for any bounded POSIX-like surface public header or wrapper that mirrors kernel syscall numbers, errno values, signal constants, wait constants, or ABI-sensitive signal frame data.

#### Scenario: Mirror constants remain synchronized
- **WHEN** source-contract validation runs
- **THEN** it detects mismatches between user-visible constants and their kernel-owned sources before runtime smoke execution
