## ADDED Requirements

### Requirement: 从当前进程复制创建子进程

BigOS SHALL support creating a new process by duplicating the current process, in addition to the existing image-based construction paths (`create_elf_user_process` / `exec_current_from_elf_image`). The duplicated child MUST be assigned a fresh PID through the existing PID allocation, linked into the parent/child and sibling chains, and made schedulable as an independent process. A child created by duplication MUST participate in the existing `wait`/`exit`/fault, zombie, and reaper teardown semantics identically to image-constructed processes.

#### Scenario: 复制路径产生独立可调度进程

- **WHEN** the current process is duplicated via the fork path
- **THEN** BigOS MUST allocate a fresh PID, link the child under the current process, and make the child an independent schedulable process with its own kernel stack
- **AND** the child MUST NOT share user low-half page-table ownership with the parent beyond intended copy-on-write leaf frames

#### Scenario: 复制子进程复用既有 wait/reap 语义

- **WHEN** a duplicated child later exits or faults
- **THEN** the child MUST transition through the existing Terminated/Faulted -> Zombie -> ReapPending -> Reaped lifecycle
- **AND** the parent MUST be able to `wait` for the child and observe its exit status exactly as for an image-constructed child

### Requirement: 进程复制失败不产生半成品进程

BigOS SHALL ensure that a failed process duplication leaves no half-constructed process visible to scheduling, `wait`, or reaping. When duplication fails at any step (PID, process object, address space, page tables, or fd table), BigOS MUST roll back partial state, return a deterministic negative error to the caller, and keep the caller Running.

#### Scenario: 复制失败回滚且父进程存活

- **WHEN** any allocation during process duplication fails
- **THEN** BigOS MUST undo partial child construction and MUST NOT publish the child into the process registry, run queue, or reap chains
- **AND** the calling parent MUST remain Running with intact state and receive a deterministic negative error
