## Purpose

定义 BigOS 当前有界 POSIX-like 进程与 I/O 子集边界：覆盖进程生命周期、镜像替换、wait/reap、fd 继承、标准 fd、pipe、fd duplication、redirection、signals、time/identity、shell 命令执行、用户可见输出和错误报告，同时明确不提供完整 POSIX 进程、terminal、shell、libc、权限、SMP、async I/O 或 broad file-backed mmap 能力。
## Requirements
### Requirement: 有界 POSIX-like 进程与 I/O 子集边界

BigOS SHALL define its current UNIX-like compatibility target as a bounded POSIX-like process and I/O subset. This subset MUST cover process lifecycle, image replacement, waiting, fd inheritance, standard fd behavior, pipe, fd duplication, redirection, signals, time/identity queries, shell command execution, user-visible output, and error reporting. This subset MUST NOT claim support for sessions, terminal process groups, job control, termios, a complete permissions model, a complete POSIX process model, dynamic linking, a complete POSIX libc, SMP, async I/O, or broad file-backed `mmap`.

#### Scenario: bounded subset is documented as the compatibility target

- **WHEN** BigOS documentation or OpenSpec artifacts describe POSIX-like process and I/O behavior
- **THEN** they MUST describe the behavior as a bounded subset
- **AND** they MUST NOT imply complete POSIX process, terminal, shell, libc, permission, SMP, async I/O, or storage support

#### Scenario: simple programs can rely on the subset boundary

- **WHEN** a simple statically linked C program uses only the documented process and I/O subset
- **THEN** it MUST be able to rely on the documented syscall wrapper, fd, wait, signal, time/identity, and shell-observable behavior
- **AND** it MUST NOT require a hosted runtime, dynamic loader, shared library, complete POSIX libc, job control, or terminal process group support

### Requirement: cwd 和相对路径纳入有界 POSIX-like 子集

BigOS SHALL include per-process current directory and relative path handling in the bounded POSIX-like process and I/O subset. Simple programs MAY rely on cwd inheritance across `fork`, preservation across `execve`, `chdir`/`getcwd` wrappers, POSIX-style `.`/`..` component handling in the supported directory tree, and relative path resolution for supported path-taking operations. This subset MUST remain explicitly bounded and MUST NOT claim sessions, terminal process groups, job control, mount namespaces, `chroot`, symlink traversal, complete POSIX pathname canonicalization, dynamic linking, SMP, async I/O, or complete POSIX libc.

#### Scenario: 简单程序依赖 cwd 子集

- **WHEN** a simple statically linked C program uses only documented cwd wrappers and path-taking file wrappers
- **THEN** it MUST be able to rely on kernel cwd resolution, libc errno translation, and fd/VFS behavior within the bounded subset
- **AND** it MUST NOT require hosted runtime, dynamic loader, full POSIX libc, namespace support, or symlink support

#### Scenario: fork 和 exec 组合保持 cwd

- **WHEN** shell or a parent process changes cwd, forks a child, and the child execs a packaged static user program
- **THEN** the child program MUST observe cwd according to the documented inheritance and exec preservation rules
- **AND** the behavior MUST remain independent of process groups, sessions, or terminal control

#### Scenario: pwd 展示有界 cwd 子集

- **WHEN** a user executes the packaged `/bin/pwd` tool after changing cwd
- **THEN** the tool MUST display the current directory through the documented libc/kernel cwd contract
- **AND** the output MUST NOT imply symlink-aware `realpath`, mount namespace, or complete POSIX utility behavior

#### Scenario: 文档不扩大兼容承诺

- **WHEN** BigOS documentation or OpenSpec artifacts describe relative path support
- **THEN** they MUST describe it as a bounded POSIX-like subset
- **AND** MUST NOT imply full POSIX filesystem, permissions, namespace, symlink, shell, or terminal semantics

### Requirement: cwd 行为验证纳入组合路径

BigOS SHALL extend behavior-oriented validation for the bounded POSIX-like process and I/O subset to cover current-directory behavior. Validation MUST include process lifecycle composition, fd/path operation composition, shell consumption, user-visible error reporting, and environment-dependent skip records.

#### Scenario: validation observes shell and process cwd

- **WHEN** process/I/O subset runtime validation runs in a configured emulator environment
- **THEN** it MUST observe at least one combined shell `cd`, POSIX-style `..`, relative path operation, `/bin/pwd`, `fork`/`execve` inheritance, and user-visible output/error path
- **AND** the result MUST be decidable from runtime output, exit status, serial/log output, or another deterministic low-level signal

#### Scenario: unavailable environment-dependent validation is recorded

- **WHEN** QEMU, Bochs, the x86_64 cross-toolchain, display/ROM dependencies, disk image configuration, or timeout oracle are unavailable
- **THEN** the corresponding cwd runtime validation MAY be skipped
- **AND** the validation record MUST identify the missing condition, substitute checks that were run, and remaining risk

### Requirement: 进程生命周期与镜像替换组合语义稳定

BigOS SHALL provide bounded process lifecycle behavior for simple user programs. A user process MUST be able to exit with a status, be waited for by an eligible parent, be reaped without leaving a reusable process table slot permanently occupied, and replace its image through the existing static user ELF execution path while preserving the documented argument, environment, fd inheritance, and error semantics.

#### Scenario: parent observes child exit status

- **WHEN** a parent process starts a child that exits with a bounded status value
- **THEN** a wait operation by the eligible parent MUST observe the child completion
- **AND** the observed status MUST reflect the child exit status according to the documented bounded wait encoding

#### Scenario: exec replaces the current image

- **WHEN** a user process successfully performs image replacement with a packaged static user program
- **THEN** the process MUST enter the replacement program with documented arguments and environment
- **AND** inherited file descriptors that are not explicitly closed by the process MUST remain available according to the subset boundary

#### Scenario: failed exec reports an error without corrupting the caller

- **WHEN** image replacement fails because the target is missing, invalid, or outside the supported user ELF subset
- **THEN** the syscall wrapper MUST report failure through the documented return value and errno path
- **AND** the caller MUST remain a valid user process unless the failure occurs after a documented no-return transition point

### Requirement: fd inheritance, duplication, redirection, and standard streams are composable

BigOS SHALL provide bounded fd semantics that compose across process creation, image replacement, shell command execution, and simple C programs. fd 0, fd 1, and fd 2 MUST represent the documented standard input, output, and error paths when present. fd duplication and redirection MUST affect only the intended fd mappings, MUST preserve unrelated open descriptors, and MUST expose failures through documented errno behavior.

#### Scenario: child inherits standard descriptors

- **WHEN** a shell or parent process starts a simple user program without explicit redirection
- **THEN** the child program MUST inherit usable standard fd mappings for input, output, and error where the parent provides them
- **AND** output written to stdout or stderr MUST be observable through the current console, shell, or serial/log validation path

#### Scenario: duplication preserves the referenced open file description

- **WHEN** a process duplicates an open fd into another fd slot
- **THEN** the duplicated fd MUST refer to the same bounded underlying object as the source fd
- **AND** closing one duplicate MUST NOT invalidate other still-open duplicates

#### Scenario: redirection changes only the target command fd mapping

- **WHEN** the shell launches a command with supported redirection syntax
- **THEN** the target command MUST observe the redirected fd mapping for the affected standard stream
- **AND** unrelated fd mappings in the shell or parent path MUST remain valid after the command setup succeeds or fails

### Requirement: pipe I/O supports bounded command composition

BigOS SHALL support bounded pipe I/O for simple process and shell composition. A pipe MUST provide separate read and write endpoints, MUST transfer bytes in FIFO order within the documented buffering limits, MUST expose EOF when all write endpoints are closed, and MUST report endpoint or capacity failures through documented error behavior instead of corrupting unrelated fd state.

#### Scenario: pipe transfers data between related processes

- **WHEN** one process writes bytes to a pipe write endpoint and another process reads from the matching read endpoint
- **THEN** the reader MUST observe the bytes in FIFO order within the bounded pipe semantics
- **AND** the operation MUST NOT require async I/O, SMP, job control, or terminal process group support

#### Scenario: closing all writers exposes EOF to readers

- **WHEN** all write endpoints for a pipe have been closed
- **THEN** subsequent reads from the remaining read endpoint MUST observe documented EOF behavior after buffered data is consumed
- **AND** unrelated file descriptors MUST remain usable

#### Scenario: shell pipe command composition is observable

- **WHEN** the shell launches a supported command composition that connects stdout of one command to stdin of another through a pipe
- **THEN** data produced by the upstream command MUST be available to the downstream command through the pipe
- **AND** the combined behavior MUST be observable through command output, exit status, or deterministic validation logs

### Requirement: signals, time, and identity remain bounded user-visible primitives

BigOS SHALL expose bounded signals, time, and identity primitives as part of the POSIX-like process/I/O subset. Signal delivery MUST remain within the implemented user process model, time queries MUST return documented monotonic or wall-clock values supported by the current kernel, and identity queries MUST return stable bounded identity values. These primitives MUST NOT imply a complete POSIX permission model, process group model, session model, or terminal control model.

#### Scenario: signal delivery is observable by a user process

- **WHEN** a user process receives a supported signal under the documented signal subset
- **THEN** the process MUST observe the signal through the implemented delivery or termination semantics
- **AND** the behavior MUST remain bounded to the current single-core process model

#### Scenario: time query returns a documented value

- **WHEN** a simple C program invokes a supported time syscall wrapper
- **THEN** the program MUST receive a value consistent with the documented BigOS time primitive
- **AND** the query MUST NOT require hosted OS services or external clock infrastructure beyond the current kernel support

#### Scenario: identity query returns bounded identity values

- **WHEN** a simple C program invokes supported identity syscall wrappers
- **THEN** the program MUST receive stable bounded identity values for the current process context
- **AND** those values MUST NOT imply complete POSIX users, groups, credentials, permissions, sessions, or process groups

### Requirement: shell command execution uses the bounded subset

BigOS SHALL make the default interactive shell a bounded consumer of the POSIX-like process/I/O subset. The shell MUST provide visible command execution behavior for packaged user programs, MUST route supported input/output/redirection/pipe behavior through documented fd and process semantics, and MUST report unsupported or failed operations through observable errors without claiming full POSIX shell compatibility.

#### Scenario: shell launches a packaged program

- **WHEN** a user enters the name of a packaged supported user program at the interactive shell
- **THEN** the shell MUST attempt to execute the program through the documented process and image replacement path
- **AND** the command output and completion MUST be observable through the text console, serial/log validation path, or deterministic runtime output

#### Scenario: shell reports unsupported command behavior

- **WHEN** a user requests a command, redirection, pipe, or syntax form outside the bounded supported subset
- **THEN** the shell MUST report failure or unsupported behavior through an observable error path
- **AND** the shell MUST NOT imply support for job control, background jobs, full POSIX shell grammar, terminal process groups, or sessions

### Requirement: 行为导向验证覆盖组合路径

BigOS SHALL provide a layered validation path for the bounded POSIX-like process and I/O subset. Validation MUST cover source/spec consistency, buildability, and runtime-observable behavior for process lifecycle, exec, wait, fd inheritance, duplication, redirection, pipe, signals, time/identity, shell command execution, and user-visible error reporting. Environment-dependent checks MAY be skipped only with an explicit record of the missing emulator, toolchain, display/ROM dependency, or disk image configuration and the remaining risk.

#### Scenario: runtime validation observes process and fd behavior

- **WHEN** the process/I/O subset runtime validation runs in a configured emulator environment
- **THEN** it MUST observe at least one combined process lifecycle, exec/wait, fd inheritance, and output/error reporting path
- **AND** the result MUST be decidable from runtime output, exit status, serial/log output, or another deterministic low-level signal

#### Scenario: runtime validation observes pipe or redirection behavior

- **WHEN** the shell or user-program validation exercises supported pipe or redirection behavior
- **THEN** the validation MUST observe that data reaches the intended fd or command endpoint
- **AND** unrelated fd state MUST remain usable after the operation

#### Scenario: unavailable environment-dependent validation is recorded

- **WHEN** QEMU, Bochs, the x86_64 cross-toolchain, display/ROM dependencies, or disk image configuration are unavailable
- **THEN** the corresponding runtime validation MAY be skipped
- **AND** the validation record MUST identify the missing condition, substitute checks that were run, and remaining risk

### Requirement: 运行时文件操作纳入有界 POSIX-like I/O 子集

BigOS SHALL 将运行时文件创建、打开、读取、写入、定位、同步、目录创建、最小目录枚举、删除和受限常规文件 rename 纳入当前有界 POSIX-like 进程与 I/O 子集。该子集 MUST 继续明确不是完整 POSIX：不提供 session、terminal process group、job control、完整权限模型、完整目录遍历、完整 POSIX `readdir/getdents` 兼容、完整目录 rename、POSIX atomic replacement、link、symlink、文件锁、async I/O、SMP、动态链接、完整 POSIX libc 或 broad file-backed `mmap`。

#### Scenario: 文档描述有界文件 I/O 子集
- **WHEN** BigOS 文档、OpenSpec 或用户程序说明描述运行时文件 I/O
- **THEN** 它们 MUST 将行为描述为有界 POSIX-like 子集
- **AND** MUST NOT 暗示完整 POSIX 文件系统、权限、目录或终端语义

#### Scenario: 简单程序可依赖子集
- **WHEN** 简单静态 C 程序只使用已文档化文件 wrapper 和 errno 语义
- **THEN** 程序 MUST 能依赖该子集在 `/rw` 中进行运行期文件操作
- **AND** 程序 MUST NOT 需要 hosted runtime、动态加载器、完整 libc 或持久文件系统

#### Scenario: 简单程序可观察目录项变化
- **WHEN** 简单静态 C 程序使用最小目录枚举观察 `/rw` 目录
- **THEN** 程序 MUST 能看到文件创建、目录创建和 unlink 后的有界目录项结果
- **AND** 程序 MUST NOT 依赖 POSIX `DIR*`、完整 `struct dirent`、排序或跨调用快照语义

### Requirement: 文件 I/O 与进程/fd 组合语义稳定

BigOS SHALL 保证运行时文件 I/O 与 `fork`、`execve`、`wait`、fd 继承、dup/dup2、pipe 和 shell 重定向组合时保持有界且可观察。继承或复制的 fd MUST 指向文档化 open file object；exec MUST 保持或关闭 fd 的行为符合 close-on-exec 规则；失败的重定向或文件打开 MUST 不破坏父进程和 shell 的无关 fd。

#### Scenario: exec 后继承文件 fd
- **WHEN** 进程打开 `/rw` 文件后 exec 一个简单 C 程序且该 fd 未标记 close-on-exec
- **THEN** 新程序 MUST 能按继承的 fd 权限继续访问该文件
- **AND** 该行为 MUST 不要求完整 POSIX 进程模型或动态链接

#### Scenario: 重定向失败不破坏 shell
- **WHEN** shell 为命令设置输入或输出重定向时文件打开失败
- **THEN** shell MUST 报告确定性错误并继续运行
- **AND** shell 自身标准 fd 和无关 fd MUST 保持可用

### Requirement: 组合式进程和 fd 行为验证可由运行时结果判定

BigOS SHALL validate representative combined behavior for the bounded POSIX-like process and I/O subset through runtime-observable results. Validation MUST cover process lifecycle, exec/wait, fd inheritance, duplication, redirection, pipe behavior, user-visible errors, and shell command composition within the documented bounded subset.

#### Scenario: exec wait 和 fd 继承组合行为可观察

- **WHEN** process/I/O behavior validation launches a supported child program through the shell or an equivalent deterministic userland path
- **THEN** validation MUST observe the child program output, parent wait result, exit status, and inherited or redirected fd behavior
- **AND** the result MUST be decidable from runtime output, file contents, fd endpoint effects, serial/log output, or another deterministic low-level signal

#### Scenario: pipe 和 redirection 端点效果可验证

- **WHEN** process/I/O behavior validation exercises supported pipe or redirection behavior
- **THEN** validation MUST observe that data reaches the intended downstream command, file, or fd endpoint
- **AND** unrelated fd state MUST remain usable after the operation

#### Scenario: unsupported shell 或 I/O 形式可见失败

- **WHEN** validation exercises a command, redirection, pipe, or syntax form outside the bounded supported subset
- **THEN** BigOS MUST report failure or unsupported behavior through an observable error path
- **AND** validation MUST NOT reinterpret unsupported behavior as successful POSIX compatibility

### Requirement: 有界用户态兼容性验证不暗示完整 POSIX

BigOS SHALL present behavior-oriented userland compatibility validation as coverage for the documented bounded process and I/O subset only. Validation artifacts MUST distinguish supported behavior from explicitly unsupported POSIX features.

#### Scenario: supported subset 被标注为有界兼容

- **WHEN** validation notes, documentation, or OpenSpec artifacts describe shell, process/fd, pipe, redirection, or filesystem behavior
- **THEN** they MUST describe the behavior as a bounded BigOS-compatible subset
- **AND** they MUST NOT imply complete POSIX process semantics, full shell grammar, sessions, terminal process groups, job control, complete permissions, dynamic linking, or a complete POSIX libc

#### Scenario: 跨 backend 规划不改变当前行为边界

- **WHEN** behavior validation is used to protect later refactoring or backend work
- **THEN** the current runnable validation target MUST remain the x86_64 Legacy BIOS/MBR/exFAT path unless a separate backend change explicitly expands it
- **AND** validation records MUST call out any backend-specific assumptions that affect runtime-observable behavior

### Requirement: Shell 可用性硬化纳入有界进程与 I/O 子集

BigOS SHALL include hardened interactive shell command composition in the bounded POSIX-like process and I/O subset. This subset MUST cover shell-visible path handling, deterministic error reporting, bounded exit-status propagation, fd inheritance, fd duplication, redirection isolation, single-pipe composition, and packaged user-program consumption. This subset MUST NOT claim support for job control, background jobs, sessions, terminal process groups, termios, complete POSIX shell language, complete POSIX process semantics, async I/O, SMP, dynamic linking, or broad file-backed `mmap`.

#### Scenario: 文档描述 shell hardening 为有界子集

- **WHEN** BigOS documentation, OpenSpec artifacts, validation notes, or roadmap follow-up describe hardened shell behavior
- **THEN** they MUST describe it as bounded shell usability within the current process/I/O subset
- **AND** they MUST NOT imply complete POSIX shell, terminal, process-group, session, permission, or job-control support

#### Scenario: 简单程序可依赖组合边界

- **WHEN** a simple statically linked user program is launched by shell through supported PATH, cwd-relative path, redirection, or single-pipe composition
- **THEN** it MUST be able to rely on documented fd inheritance, stdin/stdout/stderr mapping, errno-based failures, and wait/exit observation
- **AND** it MUST NOT require hosted runtime, dynamic loader, complete POSIX libc, process groups, sessions, terminal control, or async I/O

### Requirement: Shell 组合失败不破坏父进程 fd 与 wait/reap 语义

BigOS SHALL preserve parent shell fd state and process lifecycle safety across supported shell command composition. Failed redirection, failed pipe setup, failed fork, failed exec, and child nonzero exit MUST be observable without corrupting the parent shell's standard descriptors, leaking published file descriptors, leaving unreaped eligible children, or requiring unsafe teardown from IRQ, exception-only, scheduler-critical, or preemption-disabled contexts.

#### Scenario: 失败 setup 保留父 shell 标准 fd

- **WHEN** shell cannot complete redirection or pipe setup before launching a command
- **THEN** the parent shell MUST retain usable stdin, stdout, and stderr mappings for subsequent commands
- **AND** unpublished intermediate fd objects MUST be closed or made unreachable according to existing fd lifecycle rules

#### Scenario: 子进程完成后可回收

- **WHEN** shell launches one or two child processes for a supported external command or single-pipe command
- **THEN** shell MUST wait for eligible children and observe bounded completion status
- **AND** process table slots and fd references MUST remain reusable after completion or failure

#### Scenario: 不可阻塞上下文不执行 shell I/O setup

- **WHEN** shell-related fd/VFS, pipe, dup, wait, or exec work is required
- **THEN** it MUST run through normal user process syscall context where blocking and allocation are allowed
- **AND** it MUST NOT require IRQ-context or scheduler-critical path file I/O setup

### Requirement: bounded POSIX-like surface inventory
BigOS SHALL document and expose bounded POSIX-like user-visible POSIX-like behavior as an explicit bounded subset covering process lifecycle, wait, exec, fd I/O, pipe, dup, redirection, signal, time, identity, cwd/path, metadata, errno, and shell composition without implying complete POSIX compatibility.

#### Scenario: Bounded interface list is observable
- **WHEN** a developer reviews the bounded POSIX-like surface userland interface contract
- **THEN** the contract lists supported syscall-backed wrappers and BigOS-specific wrappers separately from unsupported POSIX features

#### Scenario: Unsupported broad POSIX features stay explicit
- **WHEN** bounded POSIX-like surface documentation or headers mention POSIX-like behavior
- **THEN** they also keep sessions, process groups, job control, termios, dynamic linking, complete POSIX libc, complete POSIX shell, and broad file-backed mmap outside the supported subset

### Requirement: bounded POSIX-like surface wait and status contract
BigOS SHALL provide a bounded process wait contract that can wait for any child or a specific child, writes a deterministic raw child status when requested, and reports unsupported wait options with a deterministic errno.

#### Scenario: Waiting for a child writes status
- **WHEN** a parent waits for an exited child with a non-null status pointer
- **THEN** the wait interface returns the child pid and writes the bounded raw exit or signal status

#### Scenario: Unsupported wait options fail predictably
- **WHEN** a user program passes unsupported wait options
- **THEN** the wait interface fails without reaping a child and reports a deterministic invalid-argument or unsupported-operation errno

### Requirement: bounded POSIX-like surface error reporting contract
BigOS SHALL provide user-visible error reporting for bounded POSIX-like interfaces through errno and stable error text suitable for shell and packaged tool diagnostics.

#### Scenario: Syscall failure maps to errno and text
- **WHEN** a supported wrapper receives a negative kernel errno result
- **THEN** it returns the wrapper-specific failure sentinel, sets positive errno, and the error text interface returns a stable non-empty message

#### Scenario: Unknown error text remains stable
- **WHEN** a user program asks for text for an unknown error number
- **THEN** the error text interface returns a deterministic fallback string without allocating memory or crashing

### Requirement: bounded libc foundation 作为有界 POSIX-like 子集消费层

BigOS SHALL treat the bounded libc foundation as a userland consumption layer over the existing bounded POSIX-like process and I/O subset. libc wrapper、headers、stdio/error reporting、file/process helpers 和 packaged tools MAY make the subset easier to consume from static C programs, but they MUST NOT expand the compatibility claim to complete POSIX process semantics, complete POSIX libc, complete shell behavior, sessions, terminal process groups, job control, termios, dynamic linking, shared libraries, async I/O, SMP, broad file-backed `mmap`, or persistent full writable filesystem semantics.

#### Scenario: libc wrapper 不扩大内核语义

- **WHEN** bounded libc foundation 为已有 process、fd、pipe、signal、time、identity、cwd 或 filesystem syscall 提供更清晰 wrapper
- **THEN** wrapper MUST preserve the documented bounded kernel/user behavior and errno translation
- **AND** wrapper MUST NOT imply unsupported POSIX semantics beyond the existing bounded subset

#### Scenario: packaged tools 使用有界子集

- **WHEN** packaged user programs or shell utilities are updated to consume bounded libc foundation helpers
- **THEN** their behavior MUST remain within the documented bounded process/I/O and filesystem subset
- **AND** tool output, diagnostics, or docs MUST NOT present the result as broad POSIX utility compatibility

#### Scenario: DIR star wrapper 不扩大目录语义

- **WHEN** bounded libc foundation introduces a `DIR*`-style directory enumeration wrapper over the existing bounded directory capability
- **THEN** the wrapper MUST preserve the documented bounded filesystem and errno behavior
- **AND** it MUST NOT imply complete POSIX directory traversal, complete `struct dirent`, ordering, snapshots, symlink traversal, mount namespace behavior, or directory fd semantics

### Requirement: bounded libc foundation 兼容性文档保持负面边界

BigOS documentation, OpenSpec artifacts, validation notes, and user-facing descriptions SHALL keep explicit negative boundaries when describing bounded libc foundation userland compatibility. They MUST state that dynamic linking、shared libraries、dynamic loader、complete POSIX libc、complete POSIX shell、job control、terminal process groups、sessions、full terminal control、broad file-backed `mmap` and async I/O remain out of scope until separate later changes intentionally add them.

#### Scenario: 文档包含非目标

- **WHEN** bounded libc foundation 文档或 validation notes 描述 libc、shell 工具或 POSIX-like wrapper 行为
- **THEN** 它们 MUST 同时标明这些行为属于 bounded subset
- **AND** MUST list or reference the relevant unsupported broad POSIX/runtime features as non-goals

#### Scenario: 验证不重新解释 unsupported behavior

- **WHEN** validation exercises a behavior outside the documented bounded libc foundation and process/I/O subset
- **THEN** unsupported, missing, or deterministic failure behavior MUST NOT be counted as POSIX compatibility success
- **AND** validation records MUST distinguish supported subset behavior from intentionally unsupported broad POSIX behavior

### Requirement: 有界显式同步纳入 POSIX-like I/O 子集
BigOS SHALL include explicit writable-backend synchronization in the bounded POSIX-like process and I/O subset. Simple static C programs and the interactive shell MAY rely on libc `sync()` and shell `sync` to request synchronization of BigOS's active writable backend dirty state through the kernel/VFS/cache path. This subset MUST remain bounded and MUST NOT imply complete POSIX `sync(2)`, `fdatasync`, async I/O, background write-back, mount namespaces, crash recovery, power-loss safety, or complete POSIX filesystem compatibility.

#### Scenario: 简单程序请求显式同步
- **WHEN** a simple static C program writes bounded data under `/rw` and calls libc `sync()`
- **THEN** BigOS MUST route the request through the documented syscall/VFS/cache synchronization path
- **AND** the program MUST observe success or errno-based failure through the libc wrapper

#### Scenario: shell 用户请求显式同步
- **WHEN** a user enters the shell `sync` builtin after writable backend mutations
- **THEN** shell MUST invoke the bounded libc/kernel synchronization path
- **AND** user-visible success or failure MUST remain observable through the shell output/error path

#### Scenario: 文档不扩大 POSIX 承诺
- **WHEN** BigOS documentation, OpenSpec artifacts, validation output, user headers, or shell help describe explicit synchronization
- **THEN** they MUST describe it as a bounded BigOS writable-backend synchronization subset
- **AND** they MUST NOT imply complete POSIX global sync, crash recovery, power-loss safety, async I/O, or broad storage/device support
