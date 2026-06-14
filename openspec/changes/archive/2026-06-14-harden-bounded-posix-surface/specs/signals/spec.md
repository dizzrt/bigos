## ADDED Requirements

### Requirement: User signal handler return path
BigOS SHALL provide a bounded user signal handler return path that routes handler completion through a libc-owned trampoline and `SYS_SIGRETURN`, restoring the interrupted user context or failing through the existing diagnostic path when the signal frame is invalid.

#### Scenario: Handler returns to interrupted context
- **WHEN** a process installs a handler for a supported signal and the handler returns normally
- **THEN** control transfers through the signal trampoline to `SYS_SIGRETURN` and resumes the interrupted user context with the expected signal mask state

#### Scenario: Invalid signal frame fails safely
- **WHEN** `SYS_SIGRETURN` observes an invalid or unmapped user signal frame
- **THEN** the kernel rejects the return through the existing bounded process-failure behavior instead of continuing with corrupted register state

### Requirement: Signal wrapper and kernel contract alignment
BigOS SHALL keep the user `sigaction` and `sigprocmask` data layout aligned with the kernel signal dispatch contract, including supported signal numbers, handler disposition, mask updates, and old-action or old-mask outputs.

#### Scenario: sigaction reports old disposition
- **WHEN** a user program installs a new disposition and requests the previous one
- **THEN** the old disposition returned to user space reflects the process state before the update

#### Scenario: sigprocmask reports old mask
- **WHEN** a user program updates the signal mask and requests the previous mask
- **THEN** the old mask returned to user space reflects the process state before the update
