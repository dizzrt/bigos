## Context

BigOS 当前的 bounded userland baseline 已经把若干 UNIX-like 基础能力串联起来：进程生命周期、用户 ELF 装载、`execve`、`fork`/COW、wait/reap、fd/VFS、pipe、dup/dup2、signals、time/identity、最小 libc wrapper、PID-1 init 和 `/bin/sh`。Stage 23 的设计重点不是新增完整 POSIX，而是把这些分散能力稳定为一个明确的组合契约。

受影响子系统跨越 `kernel/core/proc`、`kernel/core/syscall`、`kernel/core/fs`、pipe/dup 支持、signals、time/identity、`user` 中的最小 libc/init/shell/小型用户程序，以及运行时行为验证。该 change 不改变 boot 地址、链接地址、页表布局、IDT/syscall vector、磁盘布局、CR3 切换假设、用户 ELF 装载 ABI 或现有 `int 0x80` 寄存器约定。

## Goals / Non-Goals

**Goals:**

- 定义 BigOS 当前可承诺的 bounded POSIX-like process/I/O subset，并让规格覆盖组合行为而不是单点实现细节。
- 稳定 process lifecycle、image replacement、wait/reap、fd inheritance、standard fd、pipe、duplication、redirection、signals、time/identity 和 shell command execution 的可观察语义。
- 保持简单静态 C 程序与 `/bin/sh` 通过最小 libc wrapper 使用同一套错误返回、errno 和 fd 语义。
- 为后续运行时文件系统可用性和行为导向验证阶段提供可回归的用户态兼容基线。

**Non-Goals:**

- 不实现 session、terminal process group、job control、termios、完整权限模型、完整 POSIX process model 或完整 shell 语言。
- 不引入 dynamic linking、shared libraries、完整 POSIX libc、hosted runtime、SMP、async I/O、broad file-backed `mmap` 或新的 boot/storage/architecture backend。
- 不扩大当前 writable runtime storage 的持久性承诺，也不把 RAM-backed `/rw` 描述为完整持久文件系统。
- 不修改底层硬件地址、链接布局、IDT vector、syscall ABI、页表自映射布局或磁盘镜像布局。

## Decisions

- 决策：新增一个聚合 capability `posix-like-process-io-subset`，不修改所有底层 capability。
  备选方案是分别修改 `process-lifecycle`、`fd-vfs-shell`、`pipe-ipc`、`signals`、`wall-clock-time`、`process-identity-permissions` 和 `user-shell`。这种做法会把 Stage 23 的“组合边界”拆散并增加归档后的重复维护成本。聚合 capability 更适合表达当前阶段的 bounded UNIX-like 兼容目标，同时底层 specs 继续承载各自局部行为。

- 决策：以运行时可观察行为定义稳定性，而不是声明源码结构或实现入口。
  备选方案是把具体函数、文件或验证 marker 写入规格，但这会违背 roadmap 和 OpenSpec 中对项目规划层与实现细节分离的要求。当前规格只要求用户程序、shell、fd/pipe、wait、signals 和错误传播能通过构建、emulator 输出、退出状态或确定性日志被观察。

- 决策：fd 语义围绕继承、duplication、redirection 和 pipe 的组合路径收敛。
  备选方案是仅把 fd/VFS 作为单独 open/read/write 行为处理，但 Stage 23 关注 shell 命令执行和进程组合路径。实现阶段应确保 child process 在 `fork`/`execve` 后保留预期 fd 映射，dup/dup2 不破坏无关 fd，pipe endpoint 生命周期在 bounded 范围内可预测，redirection 能影响目标命令的标准输入/输出/错误流。

- 决策：错误语义保持 syscall wrapper 与最小 libc 的薄兼容层。
  备选方案是让用户程序直接依赖内核负 errno 或 kernel-private failure code，但这会削弱简单 C 程序兼容目标。用户态接口继续以 POSIX-like 的 `-1`/失败哨兵和正 `errno` 暴露错误；内核仍保留当前有界 `int 0x80` ABI。

- 决策：shell command execution 只承诺 bounded interactive path。
  备选方案是把 shell 提升为完整 POSIX shell，但这会引入 job control、terminal process groups、完整语法、background jobs 和 pipeline 兼容性等过大范围。当前只稳定可见 prompt、输入回显、命令查找/执行、基础重定向、pipe 组合和输出可观察性。

## Risks / Trade-offs

- [Risk] POSIX-like 命名可能被误解为完整 POSIX 兼容。→ Mitigation: 在 proposal、design、spec 和后续文档中反复使用 bounded subset，并列出 session、job control、terminal process group、完整权限模型等 non-goals。
- [Risk] 聚合 capability 可能与底层 specs 产生重复描述。→ Mitigation: 聚合 spec 只描述跨子系统组合行为和边界，底层能力继续描述局部实现契约；归档时避免复制底层全部细节。
- [Risk] shell、fd、pipe 和 process 行为互相耦合，局部修复可能造成组合路径回归。→ Mitigation: tasks 中加入组合行为用例和分层 runtime smoke，覆盖 fork/exec/wait、fd inheritance、dup/redirection、pipe、signals 和错误输出。
- [Risk] 运行时验证依赖本地 QEMU/Bochs、交叉工具链和磁盘镜像环境。→ Mitigation: 验证记录分离源码/构建/OpenSpec 检查与 emulator smoke；环境不可用时明确阻塞原因、替代检查和残余风险。
- [Risk] fd/pipe 生命周期错误可能导致泄漏、阻塞或错误 EOF 行为。→ Mitigation: 实现阶段复核 endpoint refcount、close-on-exec 边界、dup 后关闭顺序、wait/reap 清理路径和失败回滚。

## Migration Plan

1. 盘点当前 process/syscall/fd/VFS/pipe/signals/time/identity/shell/libc 行为，确认哪些已经满足 bounded process/I/O subset，哪些需要补齐或修正。
2. 以最小实现变更稳定组合路径：fork/exec/wait、fd inheritance、standard fd、dup/dup2、redirection、pipe endpoint、signal delivery、time/identity wrapper 和 shell command execution。
3. 增加或更新用户态行为用例，使简单 C 程序和 shell 能观察成功路径、失败路径、errno、输出和退出状态。
4. 运行最窄可用构建、OpenSpec 校验和分层 emulator 行为检查；无法运行的环境依赖检查记录原因和风险。
5. 如实现中发现某能力会暗示完整 POSIX，优先缩小本阶段接口、记录 non-goal，或拆出后续 change。

Rollback strategy: 若组合路径引入回归，优先回退新增 shell/libc/user-program 行为用例或受影响 syscall wrapper，保留既有 boot、syscall vector、用户 ELF ABI、内存布局和底层 fd/VFS/process 基线不变。

## Resolved Decisions

- 本阶段行为断言是拆成专门 process/I/O subset smoke，还是扩展现有 userland smoke 作为组合验证入口，在实现阶段根据测试复杂度决定。
- 如果现有 shell 已支持的 redirection 或 pipe 语法小于规格目标，优先补齐 shell 行为，使实现匹配本 change 的 bounded process/I/O subset 规格。
