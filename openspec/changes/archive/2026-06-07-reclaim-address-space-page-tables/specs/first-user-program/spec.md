## MODIFIED Requirements

### Requirement: 最小用户进程模型

BigOS SHALL provide a minimal single-core user process abstraction that owns a user address-space root, user entry point, user stack range, associated kernel execution context, lifecycle state, and exit status. This abstraction SHALL be sufficient to run one bounded first user program and SHALL NOT imply fork/exec/wait, signals, SMP, or general multi-process fairness. After the process is terminated or faulted, BigOS SHALL expose a safe lifecycle boundary that allows a later teardown path to release the process-owned address space, user pages, dynamic page-table pages, and kernel stack without freeing resources still active on the current return path.

#### Scenario: 创建首个用户进程

- **WHEN** kernel initialization or a default-off smoke path creates the first user process
- **THEN** the process record MUST contain a stable process id or equivalent bounded identity, a derived user address-space root, a user entry address, a user stack top/range, and a lifecycle state
- **AND** creation MUST occur in non-interrupt context and MUST NOT allocate process objects from IRQ handlers
- **AND** the process record MUST identify the resources that are owned by the process and eligible for later teardown

#### Scenario: 进程退出状态被记录

- **WHEN** the first user process calls the exit syscall or is terminated by a controlled user fault
- **THEN** BigOS MUST record a deterministic terminated state and exit code or fault reason
- **AND** BigOS MUST NOT immediately reclaim the currently active kernel stack or process object on the same return path
- **AND** BigOS MUST make the process eligible for a later safe teardown path once execution has switched to a safe kernel context

#### Scenario: 进程资源在安全上下文释放

- **WHEN** a terminated or faulted first user process is reaped from a safe non-IRQ kernel context
- **THEN** BigOS MUST release the process-owned user address-space root, user code/data/stack pages, dynamic user page-table pages, and process kernel stack
- **AND** it MUST defer or fail safely if the target kernel stack or user root is still active

### Requirement: 用户态 syscall write/exit 闭环

BigOS SHALL allow the first user program to invoke a minimal syscall set from ring3 through the established syscall ABI, including a bounded write capability and a process exit capability. The exit capability SHALL mark the process as no longer runnable and arrange safe resource teardown without freeing the active syscall stack or active address-space root before leaving the unsafe return path.

#### Scenario: 用户 write 输出确定性 marker

- **WHEN** the first user program invokes the write syscall with a user buffer containing the expected smoke payload
- **THEN** the kernel MUST validate or safely copy the bounded user buffer before reading it
- **AND** the kernel MUST emit a deterministic `BIGOS_USER_` or `BIGOS_SYSCALL_` marker to the configured diagnostic sink

#### Scenario: 非法用户 buffer 不破坏内核

- **WHEN** the write syscall receives an unmapped, kernel-space, non-user, or overlong user buffer
- **THEN** BigOS MUST return a deterministic error or terminate the current user process
- **AND** BigOS MUST NOT treat the failure as a successful write or read arbitrary kernel memory
- **AND** if the failure terminates the process, BigOS MUST use the same safe teardown boundary as controlled user faults

#### Scenario: 用户 exit 终止进程

- **WHEN** the first user program invokes the exit syscall with an exit code
- **THEN** BigOS MUST record the exit code and mark the process terminated
- **AND** the syscall MUST NOT return to the terminated user instruction stream
- **AND** the syscall path MUST NOT immediately free the active kernel stack, active CR3 root, or current process object before switching to a safe teardown context

### Requirement: 用户态 fault 受控处理

BigOS SHALL distinguish user-mode faults from kernel faults for the first user program and handle user faults with deterministic diagnostic or termination behavior without implementing demand paging or signal delivery. A user fault that terminates the current process SHALL mark the process faulted or reap-pending and leave resource reclamation to a safe teardown path.

#### Scenario: 用户态页错误被识别

- **WHEN** a page fault occurs while the interrupted context is CPL3
- **THEN** BigOS MUST recognize it as a user-mode fault using the saved code segment or equivalent privilege information
- **AND** BigOS MUST emit a deterministic user fault marker or terminate the current user process
- **AND** if the process is terminated, BigOS MUST record a fault reason and make it eligible for later safe teardown

#### Scenario: 内核页错误仍保持诊断优先

- **WHEN** a page fault occurs while the interrupted context is kernel mode
- **THEN** the existing diagnostic-only kernel page fault behavior MUST remain intact
- **AND** BigOS MUST NOT silently recover kernel faults as if they were user faults

#### Scenario: 不实现 demand paging

- **WHEN** a user page fault is caused by an unmapped user page
- **THEN** BigOS MUST NOT allocate pages on demand or resume the faulting instruction as a successful demand-paging recovery
- **AND** the fault path MUST record the failure or terminate the process
- **AND** any resulting resource release MUST occur only through the safe teardown boundary
