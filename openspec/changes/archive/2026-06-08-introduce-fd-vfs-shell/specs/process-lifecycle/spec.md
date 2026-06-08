## ADDED Requirements

### Requirement: 进程拥有 fd table
BigOS SHALL extend the normal process lifecycle core so each process owns a bounded file descriptor table. The fd table SHALL be initialized during process creation, remain associated with the process across normal scheduling, and be released through process lifecycle teardown without requiring smoke-only user program configurations.

#### Scenario: 进程创建发布 fd table
- **WHEN** a process object is created and published into the process table
- **THEN** BigOS MUST initialize the process fd table before user execution can observe the process
- **AND** fd table initialization failure MUST abort process publication and release any partially allocated fd resources

#### Scenario: 进程查找当前 fd table
- **WHEN** syscall handling for the current process needs to open, read, or close a descriptor
- **THEN** BigOS MUST resolve the fd table from the current process object rather than global singleton file state
- **AND** it MUST reject fd syscalls when no current process owns the syscall context

### Requirement: exec 保留或关闭 fd
BigOS SHALL define file descriptor behavior across `exec`. Descriptors not marked close-on-exec MUST remain associated with the process across a successful exec commit; descriptors marked close-on-exec MUST be closed before entering the new user image.

#### Scenario: exec commit 保留普通 fd
- **WHEN** a process successfully commits a new bounded ELF64 image through `exec`
- **THEN** BigOS MUST preserve open fd table entries that are not marked close-on-exec
- **AND** their file offsets and readable state MUST remain stable for the new image

#### Scenario: exec commit 关闭 close-on-exec fd
- **WHEN** a process successfully commits a new image and an fd table entry is marked close-on-exec
- **THEN** BigOS MUST close that descriptor exactly once before the new user instruction stream begins

#### Scenario: exec rollback 不破坏旧 fd table
- **WHEN** exec fails before commit and the old image remains runnable
- **THEN** BigOS MUST preserve the old process fd table unchanged except for explicitly completed operations before the exec attempt

### Requirement: exit 和 reaper 回收 fd
BigOS SHALL integrate fd table cleanup with process exit, fault termination, zombie/reap transitions, and safe reaper teardown. Cleanup MUST avoid freeing file state from unsafe active syscall, exception, IRQ, active kernel stack, or active CR3 paths.

#### Scenario: exit 后 fd 最终关闭
- **WHEN** a process exits or faults and later reaches the safe reaper boundary
- **THEN** BigOS MUST close all remaining fd table entries exactly once and release open file references before the process object becomes fully reaped

#### Scenario: wait 可观察状态不依赖 fd
- **WHEN** a parent waits for a child that has exited with open descriptors
- **THEN** BigOS MUST preserve the deterministic exit or fault status for `wait`
- **AND** fd cleanup MUST NOT corrupt parent-visible wait status or reap unrelated processes

#### Scenario: unsafe path 不释放 fd backing state
- **WHEN** process termination runs on the current process kernel stack, under the current process CR3, in IRQ context, or in a nonblocking scheduler critical section
- **THEN** BigOS MUST defer fd-backed file object destruction to a safe kernel context
