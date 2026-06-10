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
