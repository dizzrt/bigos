## MODIFIED Requirements

### Requirement: 用户地址空间 teardown 释放 owned 资源

BigOS SHALL provide a user address-space teardown helper that releases resources owned by a terminated, faulted, exec-replaced, or reaped user process: user leaf physical pages, user low-half dynamic page-table pages, the user PML4 root, process kernel stack, and process-lifecycle-owned image metadata when they are no longer active. The helper MUST preserve borrowed kernel high-half mappings and MUST run only in a safe kernel context after process lifecycle rules permit final teardown.

#### Scenario: teardown 只遍历用户 owned 低半区

- **WHEN** BigOS tears down a user address-space root for a terminated, faulted, or exec-replaced process image
- **THEN** it MUST traverse only user-owned low-half mappings and page-table pages for that process
- **AND** it MUST NOT recursively free copied high-half kernel mappings, direct-map mappings, KVMEM mappings, recursive self-mapping entries, kernel image mappings, or boot handoff page tables

#### Scenario: 用户 leaf page 被释放

- **WHEN** teardown removes a process-owned user code, data, BSS, argument, environment, or stack mapping
- **THEN** BigOS MUST clear the user leaf PTE, invalidate the affected translation according to the single-CPU boundary, and return the owned physical page to the appropriate allocator
- **AND** it MUST NOT free physical pages that are only borrowed or shared by kernel mappings

#### Scenario: PML4 root 最后释放

- **WHEN** all owned low-half user mappings and dynamic child page-table pages have been released
- **THEN** BigOS MUST release the user PML4 root frame
- **AND** it MUST do so only after the active CR3 no longer points at that root

#### Scenario: process kernel stack 安全释放

- **WHEN** a terminated, faulted, or reaped process is being released
- **THEN** BigOS MUST release that process kernel stack only if the current stack pointer is not within the target stack range
- **AND** it MUST defer or fail safely if the process stack is still the active execution stack

#### Scenario: wait 状态消费后允许最终释放

- **WHEN** a zombie process has parent-visible status that has not yet been consumed by wait
- **THEN** BigOS MUST preserve enough process table and status metadata for the parent to observe the child result
- **AND** final process object and PID release MUST wait until status consumption or explicit orphan-reap policy allows it

### Requirement: 退出和用户 fault 只安排安全回收

BigOS SHALL separate user termination from resource reclamation. `SYS_EXIT`, user-mode page fault handling, invalid user-buffer handling, exec commit failure, and child termination MUST mark the affected process terminated, faulted, zombie, or reap-pending and arrange a later safe teardown; they MUST NOT immediately free the current kernel stack, active user root, process table entry, or process object on the same unsafe return path.

#### Scenario: SYS_EXIT 标记待回收

- **WHEN** a user process invokes `SYS_EXIT`
- **THEN** BigOS MUST record the exit code and mark the process terminated, zombie, or reap-pending according to its parent/wait ownership
- **AND** the syscall path MUST NOT return to the terminated user instruction stream
- **AND** it MUST NOT free the current process kernel stack or active CR3 root before switching to a safe kernel context

#### Scenario: 用户页错误标记 faulted

- **WHEN** a CPL3 page fault terminates the current user process
- **THEN** BigOS MUST record a deterministic fault reason and mark the process faulted, zombie, or reap-pending according to its parent/wait ownership
- **AND** it MUST preserve kernel-mode page fault diagnostic behavior for CPL0 faults
- **AND** it MUST NOT implement demand paging or resume the faulting user instruction as a successful recovery

#### Scenario: reaper 在安全上下文执行

- **WHEN** a reap-pending process or exec-replaced image is selected for teardown
- **THEN** BigOS MUST execute teardown from non-IRQ context with a safe kernel address-space root active
- **AND** it MUST verify that the resources being freed are not currently required by the executing stack, CR3 state, process table iteration, or parent wait status delivery

#### Scenario: child exit 唤醒 parent wait

- **WHEN** a child process becomes zombie while its parent is blocked in wait
- **THEN** BigOS MUST wake the parent through the kernel blocking primitive and make the child status observable
- **AND** it MUST keep resource reclamation separated from the wakeup path unless the wakeup path is already a documented safe reaper context
