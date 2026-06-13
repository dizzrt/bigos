## Purpose

定义 BigOS 早期 x86_64 系统调用入口能力：复用 kernel-owned 静态 IDT 与既有 `InterruptFrame` dispatch 框架，以 `int 0x80` 软件中断门建立一条受控的 syscall 入口路径；固定最小 syscall ABI（number、参数、返回值寄存器约定）；提供 syscall dispatch 层与未知 number 的确定性错误返回；实现诊断型 syscall 用于 ring3 之前从内核态自测入口、ABI 与 dispatch 路径；在首个用户程序 runtime 启用时允许 CPL3 通过显式放开的 syscall gate 进入内核，并提供最小用户态 `write`/`exit` 闭环。

## Requirements

### Requirement: 系统调用入口机制

BigOS SHALL provide a controlled software-triggered kernel entry path so kernel or user code can enter syscall handling without relying on external IRQs or CPU exceptions. BigOS SHALL continue to reuse the kernel-owned static IDT and existing `InterruptFrame` dispatch framework, using `int 0x80` as the syscall entry. When user mode is introduced, the syscall vector gate SHALL be allowed to accept CPL3 callers while preserving exception/IRQ/syscall dispatch separation.

#### Scenario: syscall vector 被 dispatch 识别并路由

- **WHEN** kernel or user code executes the syscall instruction sequence entering the agreed syscall vector (`VECTOR_SYSCALL = 0x80`)
- **THEN** interrupt dispatch MUST identify that vector as a syscall rather than a CPU exception or external IRQ
- **AND** dispatch MUST route control to syscall handling and return through the existing architecture return path when the syscall is expected to return

#### Scenario: syscall 路径不发送外部 IRQ EOI

- **WHEN** syscall vector 被处理
- **THEN** 该路径 MUST NOT 发送 i8259 EOI（syscall 不是外部 IRQ）
- **AND** CPU 异常、外部 IRQ 与 syscall 三类入口的 EOI 语义 MUST 保持分离不变

#### Scenario: 保持既有中断契约并显式开放 syscall gate

- **WHEN** ring3 syscall support is introduced for the first user program
- **THEN** kernel-owned static IDT, `InterruptFrame` field layout, dispatch ABI, and existing CPU exception versus external IRQ paths MUST remain unchanged
- **AND** only the syscall vector gate MAY be configured to allow CPL3 software entry; unrelated exception and IRQ gates MUST NOT be relaxed for user software entry
- **AND** required user segment and TSS/kernel-stack state MAY be introduced only as part of the controlled ring3 runtime path

### Requirement: 最小系统调用 ABI

BigOS SHALL 定义并以源码级方式固定一个最小 syscall ABI，明确 syscall number、参数、返回值与错误返回所使用
的寄存器，并在英文主文档与简体中文镜像中记录其与 `InterruptFrame` 字段的对应关系。

#### Scenario: number 与返回值寄存器约定

- **WHEN** 内核代码发起一次 syscall
- **THEN** syscall number MUST 通过约定寄存器（`rax`）传入
- **AND** syscall 返回值 MUST 通过约定寄存器（`rax`）写回，即 dispatcher 写 `InterruptFrame.rax`，调用方在
  返回后从 `rax` 读取结果

#### Scenario: 参数寄存器顺序固定

- **WHEN** syscall 带有参数
- **THEN** 参数 MUST 按文档化的固定寄存器顺序（`rdi`、`rsi`、`rdx`、`r10`、`r8`、`r9`）从 `InterruptFrame`
  对应字段读取
- **AND** ABI 与 `InterruptFrame` 字段的对应关系 MUST 在英文主文档 `docs/en/arch/syscall-entry.md` 文档化并由源码级检查断言
- **AND** 简体中文镜像 `docs/zh/arch/syscall-entry.md` MUST 保持同样的 ABI 技术事实

### Requirement: 系统调用分发与未知 number 处理

BigOS SHALL 提供一个 syscall dispatch 层，按 syscall number 路由到内核实现，并对未知 number 或非法请求返回
确定性错误码，而不崩溃或落入异常路径。

#### Scenario: 已知 number 被路由到实现

- **WHEN** dispatcher 收到一个已注册的 syscall number
- **THEN** dispatcher MUST 调用对应的内核 syscall 实现
- **AND** 实现的返回值 MUST 经返回值寄存器写回调用方

#### Scenario: 未知 number 返回确定性错误码

- **WHEN** dispatcher 收到一个未注册的 syscall number
- **THEN** dispatcher MUST 在返回值寄存器写入一个确定性的负错误码（等价 `-ENOSYS`）
- **AND** dispatcher MUST NOT 崩溃、MUST NOT 进入 CPU 异常处理路径

### Requirement: 诊断型系统调用

BigOS SHALL 实现 1~2 个诊断型 syscall，用于在 ring3 阶段之前从内核态自测 syscall 入口、ABI 与 dispatch 路径。

#### Scenario: 诊断写 syscall 输出确定性 marker

- **WHEN** 内核代码调用诊断写 syscall（`SYS_DEBUG_WRITE`）并传入内核内 bounded buffer
- **THEN** 该 syscall MUST 经现有 console/串口输出确定性 `BIGOS_` marker
- **AND** 本阶段该 syscall MAY 不校验指针（调用方为内核态），但实现 MUST 把 buffer 限制为内核内 bounded 来源，
  并在文档/设计中记录引入 ring3 后必须加用户指针与长度校验

#### Scenario: 诊断 syscall 返回值路径可验证

- **WHEN** 内核代码调用返回固定值或单调 tick 的诊断 syscall（`SYS_DEBUG_NOOP` 或 `SYS_GET_TICK`）
- **THEN** 该 syscall MUST 通过返回值寄存器返回预期值
- **AND** 该返回值 MUST 可被源码级检查或自测路径断言

#### Scenario: 诊断 syscall 遵守中断上下文契约

- **WHEN** 诊断 syscall 在 `int 0x80` 上下文中执行
- **THEN** 该 syscall 实现 MUST 只做 bounded 输出或读取
- **AND** 该 syscall 实现 MUST NOT 在该路径调用 non-IRQ-safe allocator 或执行动态内存分配

### Requirement: 本阶段不进入用户态

BigOS SHALL narrow the earlier syscall-entry phase restriction: standalone syscall-entry bring-up and ring0 smoke MAY remain kernel-only, but a later first-user-program runtime path MAY enter ring3, switch to a user address space, load a user program, and invoke the syscall entry from user mode under explicit process-runtime requirements.

#### Scenario: syscall 自测仍可从内核态触发

- **WHEN** syscall entry is tested without enabling the first user program runtime
- **THEN** the existing ring0/kernel self-test path MAY continue to trigger syscall from kernel mode
- **AND** that self-test MUST NOT by itself require loading a user ELF, switching to ring3, or switching CR3

#### Scenario: 首个用户程序可从 ring3 触发 syscall

- **WHEN** the first user program runtime is enabled and has entered CPL3
- **THEN** BigOS MAY allow the user program to trigger `VECTOR_SYSCALL = 0x80` from ring3
- **AND** the syscall gate, kernel stack return mechanism, and dispatcher MUST be initialized before the user program executes that instruction
- **AND** syscall handling MUST return to user mode only for syscalls whose semantics permit returning

#### Scenario: #PF 内核诊断语义保留

- **WHEN** syscall-entry requirements are extended for user mode
- **THEN** kernel-mode `#PF` behavior MUST remain diagnostic-only
- **AND** user-mode fault handling MUST be specified by the user process capability and MUST NOT silently recover kernel faults

### Requirement: 用户态 syscall 参数安全边界

BigOS SHALL validate or safely copy user-provided syscall pointers and bounded lengths before kernel code reads user memory on behalf of a ring3 caller.

#### Scenario: 用户 buffer 在读取前被检查

- **WHEN** a ring3 caller passes a pointer and length to a syscall such as write
- **THEN** BigOS MUST verify that the requested range is in user address space and mapped with user-accessible permissions, or use an equivalent safe copy helper that detects invalid access
- **AND** BigOS MUST bound the maximum copied or printed length for early smoke syscalls

#### Scenario: 非法用户参数返回错误或终止进程

- **WHEN** a ring3 caller passes an unmapped pointer, kernel-space pointer, non-user mapping, or overlong length
- **THEN** BigOS MUST return a deterministic negative error or terminate the current user process
- **AND** BigOS MUST NOT read arbitrary kernel memory or treat the syscall as successful

### Requirement: 用户态 write 和 exit syscall

BigOS SHALL provide a minimal user-visible syscall pair sufficient for the first user program to prove a user-to-kernel write path and a controlled process exit path.

#### Scenario: write syscall 输出用户程序 marker

- **WHEN** the first user program invokes the write syscall with a valid bounded user buffer
- **THEN** the syscall MUST emit the requested bounded content or a deterministic `BIGOS_USER_` marker through the existing console/serial diagnostic path
- **AND** the syscall MUST return a deterministic byte count or success code through the syscall return register

#### Scenario: exit syscall 不返回到已终止用户流

- **WHEN** the first user program invokes the exit syscall
- **THEN** BigOS MUST mark the current user process terminated and record the requested exit code
- **AND** BigOS MUST NOT return to the terminated user instruction stream

### Requirement: 用户态 open/read/close syscall

BigOS SHALL extend the existing `int 0x80` syscall dispatch table with user-visible `open`, `read`, and `close` operations backed by the process fd table and VFS shell. These syscalls MUST preserve the existing syscall vector, register ABI, exception/IRQ/syscall dispatch separation, and no-EOI syscall handling rule.

#### Scenario: open syscall 复制用户 path

- **WHEN** a ring3 caller invokes `open` with a user pointer to a bounded NUL-terminated path and read-only flags
- **THEN** syscall handling MUST validate and copy the path into kernel-owned memory before calling VFS
- **AND** the return register MUST contain a process-local fd on success or a deterministic negative error on failure

#### Scenario: read syscall 复制到用户 buffer

- **WHEN** a ring3 caller invokes `read(fd, buffer, length)` with a valid readable fd and valid user destination range
- **THEN** syscall handling MUST read through the fd table and VFS file object, copy at most the requested bytes into the user buffer, and return the byte count through the syscall return register

#### Scenario: close syscall 释放 fd

- **WHEN** a ring3 caller invokes `close(fd)` with a valid open descriptor
- **THEN** syscall handling MUST remove the descriptor from the current process fd table, drop the file reference, and return success deterministically

#### Scenario: fd syscall 错误不破坏现有 ABI

- **WHEN** `open`, `read`, or `close` receives an invalid user pointer, unsupported flags, overlong path, bad fd, non-readable file, or VFS/backend failure
- **THEN** syscall handling MUST return a deterministic negative error or terminate the current process through the documented user fault path
- **AND** it MUST NOT relax exception/IRQ gates, send i8259 EOI for syscall, or change the `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` register convention

### Requirement: fd syscall 阻塞上下文安全

BigOS SHALL ensure fd-related syscalls run only when blocking, allocation, and synchronous filesystem reads are permitted. The syscall layer MUST guard against fd/VFS operations from nonblocking contexts and MUST keep IRQ and scheduler critical-section rules explicit.

#### Scenario: 普通用户 syscall 可进入 VFS

- **WHEN** a user process invokes `open` or `read` from normal syscall context and the current thread may block
- **THEN** syscall handling MAY enter VFS and the read-only exFAT backend under the fd/VFS blocking contract

#### Scenario: 非阻塞上下文拒绝 fd syscall

- **WHEN** fd syscall logic is reached from IRQ context, preemption-disabled scheduler critical section, or another context that must not block
- **THEN** BigOS MUST return a deterministic error or enter a documented diagnostic path
- **AND** it MUST NOT perform blocking ATA PIO reads, enqueue wait states, or allocate VFS/file/fd objects from that context

### Requirement: 用户内存 syscall ABI

BigOS SHALL extend the `int 0x80` syscall dispatch table with minimal user memory operations needed for VMA-backed userland bring-up. The extension MUST preserve the existing syscall vector, register ABI, exception/IRQ/syscall dispatch separation, and no-EOI syscall rule.

#### Scenario: brk syscall 调整 heap

- **WHEN** a ring3 caller invokes the `brk` syscall with a requested user break address
- **THEN** syscall handling MUST route the request to the current process heap VMA policy and return the committed break or deterministic negative error through the syscall return register
- **AND** it MUST NOT change the existing `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` register convention

#### Scenario: anonymous mapping syscall 创建受限映射

- **WHEN** a ring3 caller invokes the supported anonymous mapping syscall or equivalent user-memory API entry
- **THEN** syscall handling MUST validate length, alignment, flags, permissions, VMA collisions, and allocation failure before returning a user address
- **AND** unsupported file-backed, shared, W+X, kernel-space, or overlarge requests MUST fail deterministically

### Requirement: syscall user-buffer validation uses VMA

BigOS SHALL validate user pointers passed to syscalls through VMA-backed range checks plus page-table accessibility or equivalent safe-copy checks before reading from or writing to user memory.

#### Scenario: read-only syscall buffer

- **WHEN** a syscall such as `write` or `open` receives a user source pointer and length
- **THEN** syscall handling MUST confirm the complete range is covered by readable user VMAs and accessible user mappings before copying bytes into kernel-owned memory
- **AND** invalid ranges MUST return a deterministic negative error or terminate the process through the documented user fault path

#### Scenario: writable syscall buffer

- **WHEN** a syscall such as `read` receives a user destination pointer and length
- **THEN** syscall handling MUST confirm the complete range is covered by writable user VMAs and accessible user mappings before copying bytes to user memory
- **AND** it MUST NOT write to kernel addresses, read-only VMAs, executable-only VMAs, unmapped pages, or overflowed ranges

### Requirement: 用户内存 syscall 上下文安全

BigOS SHALL ensure `brk`, anonymous mapping, and VMA-backed user-copy operations run only in contexts where allocation, blocking rules, and process lookup are valid.

#### Scenario: ordinary user syscall may allocate

- **WHEN** a current user process invokes `brk` or anonymous mapping from ordinary syscall context and blocking/allocation are allowed
- **THEN** syscall handling MAY allocate VMA metadata, user pages, and dynamic page-table pages according to the user-memory API contract

#### Scenario: nonblocking context rejects user memory syscall

- **WHEN** user memory syscall logic is reached from IRQ context, a preemption-disabled scheduler critical section, no-current-process context, or another context that must not allocate or block
- **THEN** BigOS MUST reject the operation or enter a documented diagnostic path
- **AND** it MUST NOT allocate VMA metadata, perform blocking filesystem work, or publish partial user mappings from that context

### Requirement: 系统调用入口的验证可复现

BigOS SHALL use source-level checks and default-off emulator smoke to validate syscall entry wiring, ABI register conventions, dispatch routing, unknown-number error returns, and user-mode syscall entry when the first user program runtime is enabled.

#### Scenario: 源码级检查覆盖入口与 ABI 不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover: syscall vector is recognized in dispatch and does not send EOI, number/argument/return-value register conventions, known number routing, unknown number deterministic error return, diagnostic or user-visible syscall marker/return behavior
- **AND** source-level checks MUST confirm only the syscall vector is relaxed for CPL3 software entry when user mode support is enabled

#### Scenario: 构建与 emulator 验证被记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` / cross-toolchain build, relevant `uv run pytest` source checks, and `openspec validate load-first-user-program --strict`
- **AND** if Bochs runtime smoke cannot observe syscall or user markers due to emulator, ROM, serial oracle, image lock, or interaction limits, validation MUST record the missing dependency and remaining bootability risk

### Requirement: SYS_FORK syscall ABI

BigOS SHALL extend the `int 0x80` syscall dispatch table with a `SYS_FORK` operation that duplicates the current user process. `SYS_FORK` MUST return the new child PID to the parent and `0` to the child through the syscall return register, and MUST return a deterministic negative error (negated `bigos` errno) to the parent on failure. Adding `SYS_FORK` MUST preserve the existing syscall vector, the `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` register convention, exception/IRQ/syscall dispatch separation, and the no-EOI syscall handling rule.

#### Scenario: fork syscall 被 dispatch 路由

- **WHEN** a ring3 caller issues `int 0x80` with the `SYS_FORK` number in `rax`
- **THEN** the dispatcher MUST route to the fork implementation and write the child PID (parent) or `0` (child) back into the return register
- **AND** the dispatcher MUST NOT send an i8259 EOI for the syscall and MUST NOT relax any exception or external IRQ gate

#### Scenario: fork 失败返回负 errno

- **WHEN** the fork implementation cannot create a child (allocation failure or process soft-limit reached)
- **THEN** the dispatcher MUST return a deterministic negative `bigos` errno (such as `-ENOMEM` or `-EAGAIN`) to the parent through the return register
- **AND** the unknown-number, register-convention, and dispatch-separation guarantees of the existing ABI MUST remain unchanged

### Requirement: 只读身份与时间查询 syscall ABI

BigOS SHALL 在 `int 0x80` ABI 末尾新增只读的身份/时间查询 syscall（`SYS_GET_TIME`/`SYS_GETPID`/`SYS_GETPPID`/`SYS_GETUID`/`SYS_GETGID`），紧随现有 `SYS_FORK = 10` 之后固定号位，返回值经 rax 回写，且不改变既有寄存器约定、既有 syscall 号或 EOI 语义。

#### Scenario: 新增号位紧随现有末尾

- **WHEN** 定义新的身份/时间查询 syscall 号
- **THEN** 它们 MUST 从 `SYS_FORK = 10` 之后连续编号（如 `SYS_GET_TIME = 11`、`SYS_GETPID = 12`、`SYS_GETPPID = 13`、`SYS_GETUID = 14`、`SYS_GETGID = 15`）
- **AND** 既有 syscall 号、寄存器 ABI（号 -> rax、返回值 -> rax）MUST 保持不变

#### Scenario: SYS_GET_TIME 返回当前墙钟秒

- **WHEN** 用户态以 `SYS_GET_TIME` 发起 `int 0x80`
- **THEN** 分发器 MUST 把当前墙钟 Unix 秒写入 rax 返回
- **AND** 该调用 MUST NOT 发送 i8259 EOI、阻塞或分配内存

#### Scenario: 身份查询返回当前进程字段

- **WHEN** 用户态以 `SYS_GETPID`/`SYS_GETPPID`/`SYS_GETUID`/`SYS_GETGID` 发起 `int 0x80`
- **THEN** 分发器 MUST 分别把当前进程的 pid/parent_pid/uid/gid 写入 rax 返回
- **AND** 这些调用 MUST 为只读、MUST NOT 发送 i8259 EOI、阻塞或分配内存

### Requirement: 信号相关 syscall ABI

BigOS SHALL 在 `int 0x80` ABI 末尾新增信号相关 syscall（`SYS_KILL`/`SYS_SIGACTION`/`SYS_SIGPROCMASK`/`SYS_SIGRETURN`），紧随现有 `SYS_GETGID = 15` 之后固定号位，且不改变既有寄存器约定、既有 syscall 号、向量布局、DPL 设置或 syscall 不发 EOI 的规则。

#### Scenario: 信号 syscall 号紧随现有末尾

- **WHEN** 定义新的信号相关 syscall 号
- **THEN** 它们 MUST 从 `SYS_GETGID = 15` 之后连续编号（如 `SYS_KILL = 16`、`SYS_SIGACTION = 17`、`SYS_SIGPROCMASK = 18`、`SYS_SIGRETURN = 19`）
- **AND** 既有 syscall 号、寄存器 ABI（号 -> rax、参数 -> rdi/rsi/rdx/r10/r8/r9、返回值 -> rax）MUST 保持不变
- **AND** 这些 syscall MUST NOT 发送 i8259 EOI、MUST NOT 放宽任何异常或外部 IRQ 门

#### Scenario: SYS_KILL 投递并强制权限

- **WHEN** 用户态以 `SYS_KILL` 发起 `int 0x80`，传入目标 pid 与信号号
- **THEN** 分发器 MUST 路由到 kill 实现，对合法投递在目标进程置位 pending 并返回 0
- **AND** 对目标不存在返回 `-ESRCH`、对越权返回 `-EPERM`、对非法信号号返回 `-EINVAL`，全部经 rax 回写

#### Scenario: SYS_SIGACTION 注册或查询处置

- **WHEN** 用户态以 `SYS_SIGACTION` 发起 `int 0x80`，传入信号号与新处置（默认/忽略/handler 入口）
- **THEN** 分发器 MUST 更新当前进程对该信号的处置，并在请求时回写旧处置
- **AND** 对 `SIGKILL` 等不可捕获信号设置 handler 或忽略 MUST 返回 `-EINVAL`，对非法信号号 MUST 返回 `-EINVAL`

#### Scenario: SYS_SIGPROCMASK 修改阻塞掩码

- **WHEN** 用户态以 `SYS_SIGPROCMASK` 发起 `int 0x80`，传入操作类型与信号集合
- **THEN** 分发器 MUST 按操作类型修改当前进程阻塞掩码并在请求时回写旧掩码
- **AND** 试图阻塞 `SIGKILL` 等不可阻塞信号的位 MUST 被忽略而不报错或被拒绝为 `-EINVAL`（按文档化语义之一确定性处理）

#### Scenario: SYS_SIGRETURN 恢复被中断用户上下文

- **WHEN** 用户 handler 末尾以 `SYS_SIGRETURN` 发起 `int 0x80`
- **THEN** 分发器 MUST 从用户栈信号帧恢复用户可见寄存器、受约束的 rip/rsp/rflags 与旧阻塞掩码，并经现有 iretq 路径返回信号前的用户上下文
- **AND** 它 MUST NOT 破坏 `InterruptFrame` 字段布局约定，MUST NOT 依据用户栈数据返回到内核特权上下文，MUST NOT 发送 i8259 EOI

### Requirement: 可写 I/O 与管道相关 syscall ABI

BigOS SHALL 在 `int 0x80` ABI 末尾新增可写 I/O 与管道相关 syscall（`SYS_LSEEK`/`SYS_PIPE`/`SYS_DUP`/`SYS_DUP2`/`SYS_FSYNC`/`SYS_MKDIR`/`SYS_UNLINK`），紧随现有 `SYS_SIGRETURN = 19` 之后固定号位，且 MUST NOT 改变既有寄存器约定、既有 syscall 号、向量布局、DPL 设置或 syscall 不发 EOI 的规则。涉及分配、同步块 IO 或阻塞的 syscall MUST 在进入前检查调度阻塞守卫。

#### Scenario: 新 syscall 号紧随现有末尾

- **WHEN** 定义新的可写 I/O 与管道相关 syscall 号
- **THEN** 它们 MUST 从 `SYS_SIGRETURN = 19` 之后连续编号（如 `SYS_LSEEK = 20`、`SYS_PIPE = 21`、`SYS_DUP = 22`、`SYS_DUP2 = 23`、`SYS_FSYNC = 24`、`SYS_MKDIR = 25`、`SYS_UNLINK = 26`）
- **AND** 既有 syscall 号、寄存器 ABI（号 -> rax、参数 -> rdi/rsi/rdx/r10/r8/r9、返回值 -> rax）MUST 保持不变
- **AND** 这些 syscall MUST NOT 发送 i8259 EOI、MUST NOT 放宽任何异常或外部 IRQ 门

#### Scenario: SYS_PIPE 创建管道对

- **WHEN** 用户态以 `SYS_PIPE` 发起 `int 0x80`，传入用户侧两元 fd 数组指针
- **THEN** 分发器 MUST 路由到管道创建实现，成功时把读/写端两个 fd 写回用户数组并返回 0
- **AND** 对 fd 表不足返回 `-EMFILE`、内存不足返回 `-ENOMEM`、用户指针非法返回 `-EFAULT`，全部经 rax 回写

#### Scenario: SYS_DUP/SYS_DUP2 复制 fd

- **WHEN** 用户态以 `SYS_DUP`（传 oldfd）或 `SYS_DUP2`（传 oldfd、newfd）发起 `int 0x80`
- **THEN** 分发器 MUST 返回新 fd 并令其与 oldfd 共享同一打开文件对象；`SYS_DUP2` 在 newfd 已打开时 MUST 先关闭它
- **AND** 对非法 fd 返回 `-EBADF`、对无可用 fd 返回 `-EMFILE`

#### Scenario: SYS_LSEEK 定位

- **WHEN** 用户态以 `SYS_LSEEK` 发起 `int 0x80`，传入 fd、offset、whence
- **THEN** 分发器 MUST 返回新的绝对 offset；对非法 fd 返回 `-EBADF`、对管道返回 `-ESPIPE`、对非法 whence 或溢出返回 `-EINVAL`

#### Scenario: SYS_FSYNC 落盘

- **WHEN** 用户态以 `SYS_FSYNC` 发起 `int 0x80`，传入一个指向可写文件的 fd
- **THEN** 分发器 MUST 在检查阻塞守卫后把该文件相关脏块经块缓冲缓存落盘，成功返回 0
- **AND** 对非法 fd 返回 `-EBADF`、对块 IO 失败返回 `-EIO`

#### Scenario: SYS_MKDIR/SYS_UNLINK 目录变更

- **WHEN** 用户态以 `SYS_MKDIR`（path、mode）或 `SYS_UNLINK`（path）发起 `int 0x80`
- **THEN** 分发器 MUST 在检查阻塞守卫与访问权限后执行目录项创建/删除，成功返回 0
- **AND** 对只读后端返回 `-EROFS`、权限拒绝返回 `-EACCES`、空间耗尽返回 `-ENOSPC`，并按目标状态返回 `-EEXIST`/`-ENOENT`/`-EISDIR`/`-EINVAL`

### Requirement: SYS_OPEN 与 SYS_WRITE 语义扩展为可写 I/O

BigOS SHALL 扩展既有 `SYS_OPEN` 与 `SYS_WRITE` 的语义以支持可写 I/O，但 MUST NOT 改变其 syscall 号位（`SYS_OPEN = 5`、`SYS_WRITE = 2`）、寄存器 ABI 或「syscall 不发 EOI」规则。`SYS_OPEN` MUST 接受可写/创建 flags（`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`）与 `O_CREAT` 的 mode；`SYS_WRITE` MUST 支持写入文件或管道 fd 而不再仅限控制台 fd，并在分配或进入同步块 IO/阻塞前检查调度阻塞守卫。现有只读 open 与控制台 write 行为 MUST 保持不变。

#### Scenario: SYS_OPEN 接受可写/创建 flags

- **WHEN** 用户态以 `SYS_OPEN` 传入可写或 `O_CREAT` flags 与（创建时的）mode 打开可写后端的路径
- **THEN** 分发器 MUST 在权限判定通过后返回一个可写打开文件的进程 fd
- **AND** 对只读后端的写打开返回 `-EROFS`、权限拒绝返回 `-EACCES`、空间耗尽返回 `-ENOSPC`，且只读 flags 的既有打开语义保持不变

#### Scenario: SYS_WRITE 写入文件或管道 fd

- **WHEN** 用户态以 `SYS_WRITE` 向一个指向可写文件或管道写端的 fd 写入有界用户缓冲
- **THEN** 分发器 MUST 把数据写入对应打开文件对象（文件经块缓冲缓存、管道经环形缓冲），返回实际写入字节数
- **AND** 对非法/不可写 fd 返回 `-EBADF`、对读端全关的管道返回 `-EPIPE`、对无空间返回 `-ENOSPC`，且既有控制台 fd 写行为保持不变

#### Scenario: 既有只读与控制台路径不变

- **WHEN** 用户态以只读 flags `SYS_OPEN` 或向控制台 fd `SYS_WRITE`
- **THEN** 其行为 MUST 与扩展前完全一致，号位与寄存器 ABI MUST 保持不变

### Requirement: SYS_EXECVE 用户态镜像替换 syscall

BigOS SHALL 在 `int 0x80` ABI 末尾以 append-only 方式新增 `SYS_EXECVE`，把内核内已有的当前进程镜像替换路径（`exec_current_from_elf_image` + VFS 读路径）暴露给 CPL3。`SYS_EXECVE` MUST 接受用户态参数：可执行文件 `path`、`argv`（NULL 结尾指针数组）与 `envp`；MUST 经现有 VMA-backed 用户缓冲区校验拷入内核后再使用；成功时以新镜像替换当前进程地址空间并进入新程序入口、原调用点不返回；失败时 MUST 返回确定性负 errno（如 `-ENOENT`/`-EACCES`/`-ENOEXEC`/`-EFAULT`/`-E2BIG`/`-ENOMEM`）。`SYS_EXECVE` 在分配或进入同步块 IO 之前 MUST 检查调度阻塞守卫。新增此号 MUST NOT 改动既有 syscall 号位、寄存器约定、向量/DPL 布局或「syscall 不发 EOI」规则。

#### Scenario: execve 成功替换镜像

- **WHEN** 用户进程以合法 `path`/`argv`/`envp` 调用 `SYS_EXECVE` 且目标为可加载 ELF64 `ET_EXEC`
- **THEN** 内核 MUST 经现有 ELF 装载路径用新镜像替换当前进程地址空间并进入新程序入口
- **AND** 该 syscall MUST 不返回到原调用点

#### Scenario: execve 失败返回确定性负 errno

- **WHEN** `SYS_EXECVE` 的目标不存在、不可加载、参数非法或用户缓冲区校验失败
- **THEN** 内核 MUST 返回确定性负 errno 且 MUST 保持当前进程镜像不被破坏
- **AND** 调用进程 MUST 能从该失败返回继续执行

#### Scenario: execve 用户缓冲校验与阻塞守卫

- **WHEN** `SYS_EXECVE` 读取用户态 `path`/`argv`/`envp` 或进入同步块 IO/分配
- **THEN** 内核 MUST 先经 VMA-backed 用户缓冲区校验拷入相关数据
- **AND** 内核 MUST 在分配或进入同步块 IO 前检查调度阻塞守卫

#### Scenario: append-only 不改动既有 ABI

- **WHEN** 新增 `SYS_EXECVE`
- **THEN** 既有 syscall 号位、寄存器参数/返回约定、`VECTOR_SYSCALL = 0x80`、DPL 设置与「syscall 不发 EOI」MUST 保持不变
- **AND** `SYS_EXECVE` MUST 取 ABI 末尾的新号位而不复用或重排既有号位

### Requirement: 文件系统 syscall 支撑运行时可用性

BigOS SHALL 通过现有 `int 0x80` ABI 暴露有界运行时文件系统所需的用户态操作，包括文件打开、读取、写入、定位、同步、目录创建、最小目录枚举、删除和受限 rename。新增或扩展的文件相关 syscall MUST 保持 append-only 或既有号位语义扩展，MUST NOT 改变 `VECTOR_SYSCALL = 0x80`、寄存器参数顺序、返回寄存器、DPL 设置、异常/IRQ/syscall 分离或 syscall 不发送 i8259 EOI 的规则。

#### Scenario: ABI 不因运行时文件系统改变
- **WHEN** 文件相关 syscall 被新增或扩展
- **THEN** 既有 syscall 号位、寄存器 ABI、syscall vector 和 EOI 语义 MUST 保持不变
- **AND** 新能力 MUST 通过末尾追加号位或既有 open/write 语义扩展实现

#### Scenario: 文件 syscall 返回负 errno
- **WHEN** 文件相关 syscall 因路径、权限、容量、fd、用户缓冲或后端 IO 失败
- **THEN** syscall MUST 通过返回寄存器返回确定性负 errno
- **AND** MUST NOT panic、发送 IRQ EOI 或破坏当前进程 fd table

### Requirement: 文件 syscall 校验用户 path 和 buffer

BigOS SHALL 在文件相关 syscall 读取用户 path、读源 buffer、写目标 buffer、目录枚举输出 buffer、fd 数组或 argv/envp 组合数据前，使用 VMA-backed 范围检查或等价 safe-copy 机制验证用户地址、长度、NUL 终止和溢出。非法用户输入 MUST 返回确定性 `-EFAULT`、`-EINVAL` 或终止当前用户进程的文档化错误路径。

#### Scenario: open/mkdir/unlink/rename path 先复制到内核
- **WHEN** 用户态传入 path 指针执行 `open`、`mkdir`、`unlink` 或 `rename`
- **THEN** syscall 层 MUST 在调用 VFS 前验证并复制有界 NUL 终止 path
- **AND** 过长、未终止、内核地址、未映射或不可读 path MUST 被拒绝

#### Scenario: read/write buffer 方向正确
- **WHEN** 用户态执行 `read(fd, dst, len)` 或 `write(fd, src, len)`
- **THEN** `read` MUST 验证目标用户范围可写，`write` MUST 验证源用户范围可读
- **AND** 溢出范围、内核地址、只读目标或未映射页面 MUST 被拒绝

#### Scenario: 目录枚举输出 buffer 可写
- **WHEN** 用户态执行最小目录枚举 syscall 并传入输出 buffer
- **THEN** syscall 层 MUST 验证完整输出范围为用户可写且长度不超过有界上限
- **AND** 非目录 fd、非法 fd、过小 buffer 或非法用户地址 MUST 返回确定性负 errno

### Requirement: 文件 syscall 遵守阻塞上下文守卫

BigOS SHALL 在文件相关 syscall 进入可能分配、阻塞、等待或同步块 IO 的路径前检查调度阻塞守卫。普通用户进程 syscall MAY 进入 fd/VFS 和缓存路径；不可阻塞上下文 MUST 确定性失败或进入文档化诊断路径，MUST NOT 执行同步块 IO、等待或无界分配。

#### Scenario: 普通进程上下文可执行文件 IO
- **WHEN** 当前用户进程从普通 syscall 上下文执行文件操作
- **THEN** syscall 层 MAY 进入 fd/VFS、可写后端和缓存路径
- **AND** 返回值 MUST 经 syscall ABI 写回用户态

#### Scenario: 不可阻塞上下文拒绝文件 IO
- **WHEN** 文件 syscall 逻辑在 IRQ、异常诊断、调度临界区或 preemption-disabled 不可阻塞上下文中被触达
- **THEN** BigOS MUST 拒绝该操作或进入文档化诊断路径
- **AND** MUST NOT 发布部分 fd、目录项、inode 或缓存写入

### Requirement: SYS_RENAME syscall ABI

BigOS SHALL 在 `int 0x80` ABI 末尾以 append-only 方式新增受限 rename syscall（`SYS_RENAME` 或等价命名），接收源路径和目标路径两个用户态 NUL 终止字符串指针，返回值经 rax 写回。新增该 syscall MUST NOT 改变既有 syscall 号位、寄存器参数顺序、返回寄存器、`VECTOR_SYSCALL = 0x80`、DPL 设置、异常/IRQ/syscall 分离或 syscall 不发送 i8259 EOI 的规则。

#### Scenario: rename syscall 号追加在末尾

- **WHEN** 定义受限 rename syscall 号
- **THEN** 它 MUST 取当前 syscall ABI 末尾的新号位（例如紧随 `SYS_GETCWD = 32` 的 `SYS_RENAME = 33`）
- **AND** 既有 syscall 号、寄存器 ABI 和 no-EOI 语义 MUST 保持不变

#### Scenario: rename syscall 被 dispatch 路由

- **WHEN** 用户态以 rename syscall 号发起 `int 0x80` 并传入合法源路径和目标路径
- **THEN** dispatcher MUST 路由到 rename 实现并把 VFS 结果写回 rax
- **AND** syscall 路径 MUST NOT 发送 i8259 EOI 或放宽异常/外部 IRQ gate

### Requirement: rename syscall 校验用户路径和上下文

BigOS SHALL 在 rename syscall 调用 VFS 前验证并复制源路径和目标路径。路径读取 MUST 使用 VMA-backed 范围检查或等价 safe-copy 机制，MUST 校验用户地址、长度、NUL 终止和溢出；rename 在进入可能分配、等待或同步块 IO 的路径前 MUST 检查调度阻塞守卫。

#### Scenario: 两个 path 先复制到内核

- **WHEN** 用户态调用 rename syscall 并传入 `oldpath` 与 `newpath`
- **THEN** syscall 层 MUST 在调用 VFS 前验证并复制两个有界 NUL 终止 path
- **AND** 过长、未终止、内核地址、未映射或不可读 path MUST 被拒绝

#### Scenario: rename syscall 返回确定性负 errno

- **WHEN** rename 因非法用户路径、源缺失、目标已存在、只读后端、跨后端、权限、容量、fd/VFS 或后端 IO 失败
- **THEN** syscall MUST 通过返回寄存器返回确定性负 errno
- **AND** MUST NOT panic、发送 IRQ EOI、破坏当前进程 fd table 或改变 syscall ABI

#### Scenario: 不可阻塞上下文拒绝 rename

- **WHEN** rename syscall 逻辑在 IRQ、异常诊断、调度临界区或 preemption-disabled 不可阻塞上下文中被触达
- **THEN** BigOS MUST 拒绝该操作或进入文档化诊断路径
- **AND** MUST NOT 发布部分目录项、inode 变更或缓存写入
