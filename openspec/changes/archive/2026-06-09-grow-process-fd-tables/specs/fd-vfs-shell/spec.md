## MODIFIED Requirements

### Requirement: 进程 fd table

BigOS SHALL provide each process with a file descriptor table. 该 fd table SHALL 为可增长结构，其容量由可配置软上限约束，而非编译期固定的 `MAX_FDS` 硬上限，并随进程对象生命周期分配与回收 fd 存储。The table MUST allocate descriptors deterministically, prefer the lowest available descriptor, map descriptors to open file objects, reject invalid descriptors, and release references when descriptors are closed or the process is reaped.

#### Scenario: open 分配最低可用 fd

- **WHEN** a process opens a file and its fd table is below its soft limit
- **THEN** BigOS MUST allocate a stable nonnegative fd from the process table, prefer the lowest available descriptor, and bind it to the open file object
- **AND** allocation MUST succeed even when the number of open descriptors exceeds the former fixed bound (16) while below the soft limit

#### Scenario: fd table 容量耗尽

- **WHEN** a process opens a file but allocation would exceed the configured fd soft limit, or growing the fd storage requires a kernel-heap allocation that fails
- **THEN** BigOS MUST fail open deterministically with `EMFILE`, MUST release any unpublished open file object, and MUST NOT panic

#### Scenario: close 释放 fd

- **WHEN** a process closes a valid fd
- **THEN** BigOS MUST remove that fd table entry and drop the open file reference exactly once

#### Scenario: bad fd 被拒绝

- **WHEN** a process reads or closes an fd that is unused, already closed, outside current table bounds, or not readable
- **THEN** BigOS MUST return a deterministic bad-fd error and MUST NOT access freed file state
