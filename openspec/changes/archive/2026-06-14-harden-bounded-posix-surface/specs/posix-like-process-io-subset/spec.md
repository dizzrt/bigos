## ADDED Requirements

### Requirement: bounded POSIX-like surface inventory
BigOS SHALL document and expose bounded POSIX-like user-visible POSIX-like behavior as an explicit bounded subset covering process lifecycle, wait, exec, fd I/O, pipe, dup, redirection, signal, time, identity, cwd/path, metadata, errno, and shell composition without implying complete POSIX compatibility.

#### Scenario: Bounded interface list is observable
- **WHEN** a developer reviews the bounded POSIX-like surface userland interface contract
- **THEN** the contract lists supported syscall-backed wrappers and BigOS-specific wrappers separately from unsupported POSIX features

#### Scenario: Unsupported broad POSIX features stay explicit
- **WHEN** bounded POSIX-like surface documentation or headers mention POSIX-like behavior
- **THEN** they also keep sessions, process groups, job control, termios, dynamic linking, complete POSIX libc, complete POSIX shell, and broad file-backed mmap outside the supported subset

### Requirement: bounded POSIX-like surface wait and status contract
BigOS SHALL provide a bounded process wait contract that can wait for any child or a specific child, writes a deterministic raw child status when requested, and reports unsupported wait options with a deterministic errno.

#### Scenario: Waiting for a child writes status
- **WHEN** a parent waits for an exited child with a non-null status pointer
- **THEN** the wait interface returns the child pid and writes the bounded raw exit or signal status

#### Scenario: Unsupported wait options fail predictably
- **WHEN** a user program passes unsupported wait options
- **THEN** the wait interface fails without reaping a child and reports a deterministic invalid-argument or unsupported-operation errno

### Requirement: bounded POSIX-like surface error reporting contract
BigOS SHALL provide user-visible error reporting for bounded POSIX-like interfaces through errno and stable error text suitable for shell and packaged tool diagnostics.

#### Scenario: Syscall failure maps to errno and text
- **WHEN** a supported wrapper receives a negative kernel errno result
- **THEN** it returns the wrapper-specific failure sentinel, sets positive errno, and the error text interface returns a stable non-empty message

#### Scenario: Unknown error text remains stable
- **WHEN** a user program asks for text for an unknown error number
- **THEN** the error text interface returns a deterministic fallback string without allocating memory or crashing
