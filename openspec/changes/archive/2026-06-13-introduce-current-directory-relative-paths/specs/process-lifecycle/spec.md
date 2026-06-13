## ADDED Requirements

### Requirement: 进程对象拥有 cwd 状态
BigOS SHALL extend the normal process lifecycle core so each user process owns bounded current-directory state. The cwd state MUST be initialized before user execution can observe the process, copied or assigned according to process creation policy, preserved across image replacement, and released at safe teardown. This MUST NOT require smoke-only user program configurations and MUST NOT imply namespaces, sessions, process groups, or a complete POSIX process model.

#### Scenario: process publication includes cwd
- **WHEN** a process object is created and published into the process table
- **THEN** BigOS MUST initialize cwd state before the process can execute user code
- **AND** cwd initialization failure MUST abort process publication and release any partially allocated process resources

#### Scenario: fork copies cwd independently
- **WHEN** a process forks while its cwd is set to a supported directory
- **THEN** the child process MUST receive an independent cwd state with the same path value
- **AND** later `chdir` in either process MUST NOT mutate the other process cwd

#### Scenario: exec preserves cwd on commit
- **WHEN** a process successfully commits a new bounded ELF64 image through `execve`
- **THEN** BigOS MUST preserve the existing cwd state for the new user image
- **AND** close-on-exec fd handling and VMA commit MUST NOT corrupt cwd

#### Scenario: exec rollback preserves old cwd
- **WHEN** exec fails before commit and the old image remains runnable
- **THEN** BigOS MUST preserve the old cwd unchanged
- **AND** any staging path buffers or temporary cwd-related allocations from the failed attempt MUST be released

#### Scenario: safe reap releases cwd
- **WHEN** a process exits, faults, or is reaped
- **THEN** cwd resources MUST be released exactly once at a safe teardown boundary
- **AND** release MUST NOT occur from IRQ context, an active current process stack teardown path, or a preemption-disabled nonblocking region
