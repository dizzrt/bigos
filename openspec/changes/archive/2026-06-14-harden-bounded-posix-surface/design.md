## Context

当前 BigOS 已经具备 bounded userland baseline：`int 0x80` syscall ABI、进程生命周期、`fork`/COW、`execve`、wait/exit、signals、time/identity、fd/VFS、pipe/dup、RAM-backed `/rw`、最小 libc、PID-1 init、`/bin/sh` 和小型 `/bin/*` 工具均已存在。Stage 39 的目标不是扩展成完整 POSIX，而是把这些已有表层能力打磨为一致、可观察、错误契约清楚的有界 Unix-like 环境。

现有 gap 主要集中在用户态表层和组合行为：内核已有 `SYS_SIGACTION`、`SYS_SIGPROCMASK`、`SYS_SIGRETURN`，但 libc 未公开 `signal.h` wrapper；`SYS_WAIT` 已有但用户态 `wait`/`wait_status` 命名与 POSIX-like 状态语义不够清楚；`SYS_GET_TIME` 已有但缺少更常见的 `time` wrapper；`errno` 已有但缺少 `strerror`/`perror` 这类错误文本展示；shell 已有单级 pipe 和基础重定向，但需要更明确的 fd 隔离、失败恢复和退出状态规则。

该 change 横跨 `user/libc/**`、`user/sh/**`、`kernel/core/signal/**`、`kernel/core/proc/**`、`kernel/core/syscall/**` 和 runtime smoke。它不改变 boot、linker、disk layout、interrupt vector、syscall entry、page-table layout 或默认 x86_64 Legacy BIOS 交付路径。

## Goals / Non-Goals

**Goals:**

- 为现有 signal syscall 建立最小可用的用户态 `signal.h` 表层，包括 `sigaction`、`sigprocmask`、signal 常量/类型和 handler 返回契约。
- 为现有 wait 能力建立清晰的 bounded `wait`/`waitpid` 用户态契约，明确 status 写回、unsupported options 和无子进程错误。
- 为现有 time/errno 能力增加 POSIX-like 表层，提供 `time`、`strerror`、`perror` 或等价 bounded 接口。
- 硬化 shell 的单级 pipe、基础 redirection、PATH lookup、外部命令失败、退出状态和父 shell fd 恢复行为。
- 增加可观察验证，覆盖新增 wrapper、signal handler 返回、wait 状态、错误文本、pipe/redirection 失败恢复和 bounded shell status。

**Non-Goals:**

- 不实现完整 POSIX libc、hosted stdio、locale、threads、dynamic linking、shared libraries 或动态加载器。
- 不实现 sessions、terminal process groups、job control、background jobs、完整 termios 或完整 POSIX shell grammar。
- 不实现 broad file-backed `mmap`、async I/O、持久完整 writable filesystem、link/symlink、mount namespace 或广泛 storage/device drivers。
- 不引入 SMP、新 ISA、UEFI runtime parity，且不改变 `int 0x80` syscall ABI、syscall numbers、IDT vector、InterruptFrame 布局或用户 ELF 静态链接边界。
- 不把 `bigos_readdir` 立即扩展为完整 `DIR*`/`opendir`/`readdir`/`closedir` POSIX 目录流层。

## Decisions

### 复用现有 syscall ABI，不新增 syscall number

本 change 复用现有 `SYS_SIGACTION`、`SYS_SIGPROCMASK`、`SYS_SIGRETURN`、`SYS_WAIT`、`SYS_GET_TIME` 和 `errno` 翻译路径。用户态新增内容优先落在 libc header、wrapper、trampoline 和 shell/tool 行为上。

选择理由：Stage 39 目标是表层硬化而不是内核能力扩张；现有 syscall table 已覆盖必要内核入口。保持 syscall number 和 `int 0x80` ABI 不变，可降低 boot/runtime 回归风险。

替代方案：新增 POSIX 命名 syscall，例如 `SYS_WAITPID` 或 `SYS_TIME`。该方案会扩大 ABI 面，且现有 `SYS_WAIT`/`SYS_GET_TIME` 足以支撑 bounded wrapper，因此不采用。

### Signal 表层采用 bounded `sigaction`/`sigprocmask` 加 trampoline

用户态公开最小 `signal.h`：signal number 常量、`sigset_t`、`struct sigaction`、`sigaction`、`sigprocmask`。handler 返回必须通过用户态 trampoline 调用 `SYS_SIGRETURN`，恢复内核保存的 interrupted user context。

选择理由：内核已经有 signal delivery 和 `SYS_SIGRETURN`，但没有公开 wrapper 会让用户程序无法按稳定契约安装 handler。trampoline 将架构相关返回路径封装在 libc 内，避免应用直接依赖 raw syscall 细节。

替代方案：只暴露 raw `syscall` helper，要求用户程序自行调用 `SYS_SIGRETURN`。该方案会泄漏低层 ABI，不符合 Stage 39 的表层硬化目标，因此不采用。

### Wait 表层提供 bounded POSIX-like 命名

用户态保留 BigOS-specific `wait_status(pid, status)` 兼容现有代码，同时提供 `wait(int *status)` 和 `waitpid(pid_t pid, int *status, int options)`。`wait(status)` 等价等待任意子进程；`waitpid` 初期仅支持 `options == 0`，其他 options 返回 `-1` 且设置 `errno = EINVAL` 或明确的不支持错误。

选择理由：现有 `wait(pid_t pid)` 签名容易与 POSIX 习惯冲突。提供 bounded POSIX-like 命名能改善程序可移植性，同时不要求实现 `WNOHANG`、job control 或进程组等待。

替代方案：直接破坏性替换现有 `wait(pid_t pid)`。该方案会影响已有 userland 代码，迁移风险更高，因此采用兼容迁移。

### Time 和错误文本作为 libc 层能力

`time` wrapper 基于现有 `SYS_GET_TIME`，只承诺秒级、只读、bounded wall-clock 语义。`strerror` 返回静态字符串，覆盖当前 `errno.h` 中的错误码；未知错误返回稳定 fallback。`perror` 使用现有 `write`/`fprintf` 输出到 stderr。

选择理由：这些是用户态可用性与诊断能力，不需要内核新增状态。把它们放在 libc 层符合 freestanding bounded libc 的边界。

替代方案：把错误文本放入内核或 syscall。该方案会把展示策略推入内核，不符合最小内核 ABI 方向，因此不采用。

### Shell 硬化优先定义可观察行为

Shell 继续支持 whitespace tokenization、builtin、external command、单级 pipe、`>`/`<` 基础重定向。硬化重点是：失败后父 shell 标准 fd 保持可用；pipe/redirection setup 中打开的临时 fd 全部关闭；unsupported syntax 返回 bounded status；命令查找失败、exec 失败和外部命令非零退出有一致错误展示。

选择理由：Stage 39 要改善交互可用性和小程序组合，而不是扩展 shell grammar。明确失败恢复比增加 `&&`、`;`、`>>`、`2>` 更能降低现有系统使用成本。

替代方案：扩展完整 POSIX shell 语法。该方案会引入 parser、job control、terminal semantics 等一系列后续复杂度，超出 Stage 39。

### 验证采用 source-contract 加 default-off runtime smoke

新增或扩展源级检查，确保 syscall number mirror、errno mirror、header 声明和 wrapper 一致。新增或扩展 default-off runtime smoke，覆盖 signal handler 返回、waitpid status、time/strerror/perror、pipe EOF、redirection 失败恢复和 shell status。

选择理由：这些行为需要同时验证编译期契约和运行时可观察输出。默认 off smoke 符合现有 validation 风格，不影响默认正常启动路径。

替代方案：只靠源码审查或只跑 full boot。前者无法证明运行时交互行为，后者成本过高且不利于定位，因此不采用。

## Risks / Trade-offs

- [Risk] signal trampoline 与 user stack frame ABI 不一致会导致 handler 返回后崩溃或恢复错误上下文。→ Mitigation: 将 signal frame/trampoline ABI 写入规格，增加 signal smoke 覆盖 handler 返回、mask 恢复和默认终止路径。
- [Risk] 调整 `wait` 声明可能破坏已有 userland 调用。→ Mitigation: 保留 `wait_status` 作为 BigOS-specific wrapper，分阶段引入 `wait(int *)`/`waitpid`，必要时先更新内部调用点。
- [Risk] `strerror` 文本与 kernel errno mirror 漂移。→ Mitigation: 扩展源级契约测试，确保用户态 errno 常量和错误文本覆盖当前公开 errno。
- [Risk] shell redirection/pipe 失败路径遗漏 fd close，导致父 shell stdio 污染。→ Mitigation: 把 fd 恢复和 close 顺序作为规格场景，并在 userland smoke 中验证失败后还能继续执行命令。
- [Risk] 过度 POSIX 命名可能暗示完整兼容。→ Mitigation: header 注释、OpenSpec 和 docs 必须明确 bounded subset，不承诺 job control、termios、完整 libc、完整 shell 或完整 filesystem semantics。
