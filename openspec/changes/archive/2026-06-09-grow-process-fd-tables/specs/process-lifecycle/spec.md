## MODIFIED Requirements

### Requirement: PID 与进程表有界

BigOS SHALL allocate PIDs from a deterministic bounded policy and track live, zombie, and reap-pending processes in a kernel-owned process registry. 该注册结构 SHALL 为可增长、可回收结构，承载的进程数由可配置软上限约束，而非编译期固定的 `MAX_PROCESSES` 硬上限；进程对象 SHALL 从内核堆分配并在完全 reap 后回收。PID allocation and lookup SHALL be single-core safe and SHALL NOT imply SMP migration, namespaces, process groups, sessions, or POSIX permission policy.

#### Scenario: PID 分配成功

- **WHEN** a process is created and the process registry is below its soft limit
- **THEN** BigOS MUST assign a nonzero stable PID that does not alias another live or zombie process
- **AND** the process registry MUST allow lookup by PID until the process is fully reaped

#### Scenario: 进程表容量耗尽

- **WHEN** process creation would exceed the configured soft limit of the process registry, the process-object heap allocation fails, or PID allocation cannot produce a safe identifier
- **THEN** BigOS MUST fail creation deterministically without publishing a partially initialized process, and MUST NOT panic
- **AND** any allocated address-space root, user page, kernel stack, loader buffer, or process object from the failed attempt MUST be released or marked for safe release

#### Scenario: PID 重用等待安全回收

- **WHEN** a process has exited but remains zombie or reap-pending
- **THEN** BigOS MUST NOT reuse its PID for a new process until the parent-visible status has been consumed or the process has been fully reaped by policy

#### Scenario: 回收后槽位与对象内存复用

- **WHEN** a process is fully reaped and removed from the registry
- **THEN** BigOS MUST free its heap-allocated process object after no `current`, reap-chain, or parent/child reference remains
- **AND** its registry slot and PID MUST become reusable by later process creation without corrupting parent/child or reap chains
