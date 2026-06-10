## Purpose

定义 BigOS 常规进程生命周期核心：提供非 smoke-only 的进程对象、PID 与进程表、
父子关系、wait/exit/fault/reap 状态转换、bounded exec 镜像加载边界，以及可复现
验证要求。该能力不引入 SMP、POSIX 权限、命名空间、进程组、session、动态链接、
demand paging、fd/VFS、writable filesystem 或用户态 libc。

## Requirements

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

### Requirement: 进程对象拥有 VMA 集合

BigOS SHALL extend the normal process lifecycle core so each user process image owns a bounded VMA collection. The process object MUST distinguish the active committed VMA collection from any staging VMA collection used during exec or image replacement.

#### Scenario: process publication includes VMA state

- **WHEN** a process is published into the process table with a runnable user image
- **THEN** BigOS MUST associate the process with a committed VMA collection that describes the runnable image, heap boundary, anonymous mappings, and stack policy
- **AND** syscall user-buffer validation, page fault handling, and teardown MUST resolve VMA state from the current process image rather than global singleton state

#### Scenario: process creation failure releases VMA state

- **WHEN** process creation fails after allocating VMA metadata but before publishing a runnable process
- **THEN** BigOS MUST release the VMA metadata along with owned user pages, page-table pages, kernel stack, fd table, and loader buffers allocated by the failed attempt
- **AND** it MUST NOT publish a process table entry that references a partial VMA collection

### Requirement: exec 以 staging VMA commit

BigOS SHALL build a new VMA collection in staging state during exec and publish it only when the new user image can be committed. The old VMA collection MUST remain valid until commit succeeds or the old process image is deliberately terminated.

#### Scenario: exec commit swaps VMA atomically

- **WHEN** exec validation, segment mapping, stack setup, heap setup, and fd close-on-exec handling have all succeeded
- **THEN** BigOS MUST atomically publish the new VMA collection as the process committed image state before entering the new user instruction stream
- **AND** subsequent user-buffer checks and fault handling MUST use the new committed VMA collection

#### Scenario: exec rollback preserves old VMA

- **WHEN** exec fails before commit and the old image remains runnable
- **THEN** BigOS MUST preserve the old process VMA collection unchanged
- **AND** it MUST release all staging VMA metadata and owned memory from the failed exec attempt

### Requirement: exit/fault/reaper preserves VMA safety

BigOS SHALL integrate VMA ownership with exit, user fault, zombie, wait, and safe reaper state transitions. Unsafe active paths MUST NOT free the current process VMA collection before control has moved to a safe kernel context.

#### Scenario: exit does not free active VMA immediately

- **WHEN** a user process invokes exit while running on its process kernel stack or under its user address-space root
- **THEN** BigOS MUST record exit state and arrange safe reaping without immediately freeing the active VMA collection on that return path
- **AND** parent-visible wait status MUST remain observable independent of VMA cleanup

#### Scenario: user fault marks process before VMA cleanup

- **WHEN** a user-mode page fault or invalid user-buffer operation terminates the current process
- **THEN** BigOS MUST record a deterministic fault reason and transition the process to faulted, zombie, or reap-pending state before VMA teardown
- **AND** final VMA cleanup MUST occur only through the documented safe reaper boundary

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

### Requirement: 进程身份与启动时间戳字段

BigOS SHALL 在进程对象中维护最小身份四元组 uid/gid/euid/egid 与启动墙钟时间戳，并在各创建路径下按规则初始化或继承，使进程生命周期具备可继承、可判定的身份结构。

#### Scenario: init 进程身份与时间戳

- **WHEN** PID 1 的 init 进程被创建
- **THEN** 其 uid/gid/euid/egid MUST 为 0（root）
- **AND** 其启动时间戳 MUST 取创建时刻的当前墙钟 Unix 秒

#### Scenario: ELF 创建路径初始化身份

- **WHEN** 进程通过 ELF 创建路径（非 fork）产生
- **THEN** 其 uid/gid/euid/egid MUST 默认初始化为 0（root，因当前无 login/身份变更来源）
- **AND** 其启动时间戳 MUST 取创建时刻的当前墙钟 Unix 秒

#### Scenario: fork 路径继承身份

- **WHEN** 进程通过 `fork` 产生
- **THEN** 其 uid/gid/euid/egid MUST 逐字段继承自父进程
- **AND** 其启动时间戳 MUST 取 fork 时刻的当前墙钟 Unix 秒

#### Scenario: 身份字段不破坏既有生命周期

- **WHEN** 进程进入既有的父子链接、`wait`/`exit`、zombie/reaper teardown 流程
- **THEN** 新增身份与时间戳字段 MUST NOT 改变既有进程生命周期状态机与回收语义

### Requirement: 进程携带信号状态字段

BigOS SHALL 为每个进程维护信号 pending 位图、阻塞掩码与每信号处置表字段（追加字段，不重排既有布局），并定义其在 init/ELF 创建路径下的初始化规则。

#### Scenario: 进程创建初始化信号状态

- **WHEN** 进程通过 init 或非 fork ELF 创建路径产生
- **THEN** 其每信号处置 MUST 初始化为默认动作，阻塞掩码 MUST 为空，pending 位图 MUST 为空
- **AND** 这些字段 MUST 为定长内联存储，初始化 MUST NOT 引入新的分配失败路径

### Requirement: 信号默认终止复用退出生命周期

BigOS SHALL 让信号的默认 Terminate 动作复用现有 exit/fault-to-reaper teardown 与退出状态语义，不引入独立的进程回收路径。

#### Scenario: 默认终止经 reaper 回收

- **WHEN** 一个进程因默认 Terminate 信号（或 `SIGKILL`）被终止
- **THEN** BigOS MUST 通过现有 exit/fault-to-reaper 生命周期回收其地址空间与进程资源
- **AND** MUST 把信号号编码进退出/fault 状态供父进程 `wait` 或诊断观察
- **AND** MUST NOT 改变既有 zombie/reaper、`wait_status_consumed`、`parent_waiting` 语义

### Requirement: 子进程退出向父进程投递 SIGCHLD

BigOS SHALL 在子进程进入 zombie（退出或被信号终止）时，向其父进程的 pending 位图投递 `SIGCHLD`，默认动作为 Ignore，且不改变现有 `wait`/reaper 判定。

#### Scenario: 子进程退出置位父进程 SIGCHLD

- **WHEN** 一个子进程退出或被信号终止进入 zombie
- **THEN** BigOS MUST 在其父进程 pending 位图置位 `SIGCHLD`
- **AND** 该置位 MUST NOT 分配内存，MUST NOT 改变现有 `wait` 唤醒与 reaper 回收行为
- **AND** 父进程未注册 `SIGCHLD` handler 时该信号 MUST 按默认 Ignore 处理而不改变父进程行为
