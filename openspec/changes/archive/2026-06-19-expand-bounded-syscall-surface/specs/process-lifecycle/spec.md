## ADDED Requirements

### Requirement: wait 变体遵守进程生命周期

BigOS SHALL route bounded wait variants through the existing parent-child lifecycle, zombie state, and safe reaper boundary. Waiting for a child MUST preserve deterministic exit or signal status, reap only eligible children, and avoid freeing active process resources from unsafe syscall, exception, IRQ, active-kernel-stack, or active-CR3 paths.

#### Scenario: 指定 child wait 只回收目标子进程

- **WHEN** a parent waits for a specific live child pid and that child later exits
- **THEN** BigOS MUST return that child pid with its bounded status and release that child through the safe reaper lifecycle
- **AND** siblings that are not selected by the wait MUST remain waitable

#### Scenario: no-child wait 返回确定性错误

- **WHEN** a process calls a bounded wait variant without any eligible child matching the selector
- **THEN** BigOS MUST return a deterministic no-child errno
- **AND** it MUST NOT mutate process table entries, wait queues, or previously reaped child state

### Requirement: exec close-on-exec lifecycle

BigOS SHALL apply fd close-on-exec state during successful `execve` commit. Descriptors marked close-on-exec MUST be closed exactly once before the new user instruction stream begins, while descriptors not marked close-on-exec MUST remain associated with the process across the successful exec.

#### Scenario: exec commit closes marked descriptors

- **WHEN** a process successfully commits a new user image through `execve` and one or more fd table entries are marked close-on-exec
- **THEN** BigOS MUST close those marked descriptors exactly once before entering the new image
- **AND** remaining unmarked descriptors MUST preserve their open file references and offsets

#### Scenario: exec rollback preserves fd flags

- **WHEN** `execve` fails before commit and the old image remains runnable
- **THEN** BigOS MUST preserve the old fd table entries and their close-on-exec flags
- **AND** it MUST NOT close descriptors solely because a failed exec attempt observed those flags

### Requirement: fork and dup preserve fd-control invariants

BigOS SHALL define close-on-exec flag behavior across `fork`, `dup`, and `dup2` within the bounded fd table model. `fork` MUST preserve the parent-visible close-on-exec state in the child copy, while descriptor duplication MUST use documented deterministic flag behavior that cannot leak stale closed entries or double-close an open file object.

#### Scenario: fork 继承 close-on-exec flag

- **WHEN** a process forks with open descriptors that have close-on-exec flags
- **THEN** BigOS MUST create a child fd table that observes the same close-on-exec state for inherited descriptors
- **AND** parent and child descriptor closure MUST remain independently reference-counted through existing open file object rules

#### Scenario: dup2 覆盖目标 descriptor

- **WHEN** a process duplicates one descriptor over another descriptor using bounded `dup2`
- **THEN** BigOS MUST close the target descriptor if it is open, bind it to the source open file object, and establish deterministic close-on-exec state for the new target entry
- **AND** failure MUST leave the original source descriptor and any unaffected descriptors unchanged
