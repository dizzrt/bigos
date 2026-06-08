## ADDED Requirements

### Requirement: 常规进程核心可构建

BigOS SHALL provide a normal process lifecycle core that is buildable outside smoke-only user-program configurations. The core SHALL define process identity, lifecycle state, parent/child ownership, process table membership, address-space ownership, kernel execution context ownership, and exit status without requiring `user_program_smoke` or `user_elf_smoke` to be enabled.

#### Scenario: 非 smoke 构建包含进程核心

- **WHEN** BigOS is built with user program smoke switches disabled
- **THEN** the process lifecycle core MUST still compile as a normal kernel subsystem
- **AND** smoke-specific embedded program entry, user ELF smoke launch, and diagnostic marker paths MUST remain disabled unless their explicit smoke switches are enabled

#### Scenario: 进程对象记录所有权

- **WHEN** a process object is created
- **THEN** it MUST record a stable PID, lifecycle state, parent PID or root ownership marker, child linkage, owned user address-space root, user entry/stack metadata, process kernel stack metadata, and exit/fault status storage
- **AND** it MUST distinguish owned resources from borrowed kernel high-half mappings and shared kernel runtime structures

#### Scenario: 中断上下文不创建进程

- **WHEN** IRQ, exception, timer, keyboard, or syscall dispatch code runs in a context that must not allocate process objects
- **THEN** BigOS MUST NOT allocate or free process table entries, process kernel stacks, PID records, or parent/child linkage through ordinary allocator APIs from that context

### Requirement: PID 与进程表有界

BigOS SHALL allocate PIDs from a deterministic bounded policy and track live, zombie, and reap-pending processes in a kernel-owned process table. PID allocation and lookup SHALL be single-core safe and SHALL NOT imply SMP migration, namespaces, process groups, sessions, or POSIX permission policy.

#### Scenario: PID 分配成功

- **WHEN** a process is created and the process table has capacity
- **THEN** BigOS MUST assign a nonzero stable PID that does not alias another live or zombie process
- **AND** the process table MUST allow lookup by PID until the process is fully reaped

#### Scenario: 进程表容量耗尽

- **WHEN** process creation would exceed the bounded process table capacity or PID allocation cannot produce a safe identifier
- **THEN** BigOS MUST fail creation deterministically without publishing a partially initialized process
- **AND** any allocated address-space root, user page, kernel stack, or loader buffer from the failed attempt MUST be released or marked for safe release

#### Scenario: PID 重用等待安全回收

- **WHEN** a process has exited but remains zombie or reap-pending
- **THEN** BigOS MUST NOT reuse its PID for a new process until the parent-visible status has been consumed or the process has been fully reaped by policy

### Requirement: 父子关系和 wait 语义

BigOS SHALL model parent/child relationships for created processes and provide a minimal `wait` capability that lets a parent observe child termination status and trigger safe child reaping. Waiting SHALL use the kernel blocking model and SHALL run only from contexts where blocking is allowed.

#### Scenario: 父进程等待已退出子进程

- **WHEN** a parent process waits for a child that is already zombie
- **THEN** BigOS MUST copy or return the child PID and deterministic exit or fault status to the parent-visible result
- **AND** the child MUST become eligible for final safe reaping after the status is consumed

#### Scenario: 父进程阻塞等待运行中子进程

- **WHEN** a parent process waits for an existing child that has not yet terminated
- **THEN** BigOS MUST block the parent on a wait queue or equivalent blocking primitive
- **AND** child exit or fault termination MUST wake the parent exactly once for the waitable state transition

#### Scenario: 无可等待子进程

- **WHEN** a process waits for a PID that is not its child or waits when it has no waitable children
- **THEN** BigOS MUST return a deterministic error without blocking indefinitely
- **AND** it MUST NOT reap unrelated processes

#### Scenario: 禁止在不可阻塞上下文 wait

- **WHEN** wait is invoked from IRQ context, preemption-disabled scheduler critical section, or another context that must not block
- **THEN** BigOS MUST reject the operation or enter a deterministic diagnostic path
- **AND** it MUST NOT enqueue the current thread in a wait state that cannot be safely scheduled

### Requirement: exit 和 fault 进入 zombie/reap 生命周期

BigOS SHALL handle process exit and user-mode fault termination by recording status, preventing return to the terminated user instruction stream, waking eligible waiters, and deferring resource reclamation to a safe reaper boundary.

#### Scenario: 用户 exit 记录状态

- **WHEN** a user process invokes the exit syscall with an exit code
- **THEN** BigOS MUST record the exit code, mark the process terminated or zombie, and prevent control from returning to the terminated user instruction stream
- **AND** it MUST wake a waiting parent if the parent is waiting for that child

#### Scenario: 用户 fault 记录原因

- **WHEN** a CPL3 exception or invalid user-buffer operation terminates the current process
- **THEN** BigOS MUST record a deterministic fault status and mark the process faulted or zombie
- **AND** kernel-mode fault diagnostics MUST remain distinct from user-mode process termination

#### Scenario: 当前路径不释放活动资源

- **WHEN** exit or user fault termination runs on the current process kernel stack or under the current process CR3
- **THEN** BigOS MUST NOT free the active kernel stack, active user root, current process object, or currently executing syscall/exception frame on that same unsafe path
- **AND** it MUST switch or schedule to a safe kernel context before final resource teardown

### Requirement: general exec 加载 bounded 用户镜像

BigOS SHALL provide a minimal general `exec` path that creates or replaces a process user image from a bounded ELF64 `ET_EXEC` file read through the existing read-only filesystem stack. The exec path SHALL support basic `argv` and `envp` setup and SHALL NOT require fd/VFS, writable filesystems, dynamic linking, demand paging, or user-space libc.

#### Scenario: exec 读取并验证 ELF

- **WHEN** a process lifecycle path requests exec for a configured absolute user ELF path
- **THEN** BigOS MUST read the file through the kernel read-only filesystem API in non-IRQ context, validate the ELF64 `ET_EXEC` image, and reject malformed or unsupported images before publishing a runnable user image
- **AND** hosted OS file IO and bootloader-only exFAT helpers MUST NOT be required

#### Scenario: argv/envp 布置到用户栈

- **WHEN** exec commits a new user image with basic arguments and environment strings
- **THEN** BigOS MUST copy bounded `argv` and `envp` strings into mapped user stack memory and initialize the entry stack according to the documented BigOS user entry convention
- **AND** it MUST reject argument counts, string lengths, pointer tables, or stack layouts that exceed the bounded user stack region

#### Scenario: exec commit 前失败可回滚

- **WHEN** exec fails before the new image is committed
- **THEN** BigOS MUST release all new image pages, dynamic page-table pages, loader buffers, and temporary metadata allocated for that attempt
- **AND** it MUST NOT corrupt the caller's still-active process image or process table entry

#### Scenario: exec commit 后失败受控终止

- **WHEN** exec fails after the old image can no longer be safely resumed
- **THEN** BigOS MUST terminate the affected process through the same exit/fault-to-reaper lifecycle
- **AND** it MUST record a deterministic exec failure status for parent wait or diagnostics

### Requirement: 进程生命周期验证可复现

BigOS SHALL provide reproducible validation for process lifecycle behavior, including source-level checks, normal build gating, smoke consumers, wait/exit behavior, exec argument bounds, and safe teardown evidence. Validation SHALL record unavailable toolchain or emulator dependencies explicitly.

#### Scenario: source checks 覆盖生命周期不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover process core build gating, PID uniqueness, process table capacity failure, parent/child linkage, wait wakeup, zombie-to-reap transition, exec rollback, argv/envp bounds, active-root teardown rejection, and current-stack release deferral
- **AND** checks MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, exception/IRQ gate privilege, and EOI semantics are not moved or widened

#### Scenario: runtime smoke 记录 marker 和跳过原因

- **WHEN** emulator validation is performed for process lifecycle
- **THEN** validation MUST record the selected `xmake f` switches, QEMU headless serial markers, log paths, timeout behavior, and any Bochs or QEMU+Bochs cross-validation used
- **AND** if QEMU, Bochs, cross-binutils, ROM/display configuration, image generation, or serial observability is unavailable, validation MUST record the skipped case, substitute checks, and residual bootability risk
