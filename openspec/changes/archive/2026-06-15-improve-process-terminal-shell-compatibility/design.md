## Context

BigOS 当前默认启动路径已经进入 resident init 并启动 `/bin/sh`，用户态可通过现有 fd/syscall、TTY/console、process lifecycle、signals、pipe/dup、cwd/PATH 和路径工具进行基本交互。Stage 42 不再新增一个大型底层设施，而是把这些已存在能力组合成更稳定的交互契约：父 shell 能可靠等待子命令，信号终止能被观察，默认终端控制输入能以有界方式影响读行，pipe/重定向失败不会破坏父 shell fd 状态。

该 change 跨越 `kernel/core/proc`、`kernel/core/signal`、`kernel/core/terminal`、`kernel/core/fs`、`kernel/core/syscall` 与 `user/**`，但不改变 boot、linker、page-table、disk image 或 syscall vector。所有运行时行为仍以 x86_64 Legacy BIOS/MBR/exFAT baseline、单核、同步、有界用户态和静态 user ELF 为前提。

控制流边界：

- Keyboard IRQ1 只生产有界字符或控制输入事件，最多通过 IRQ-safe 路径唤醒等待者，不执行 echo、shell policy、文件系统或动态分配。
- 非中断 TTY/terminal consumer 从默认终端 stdin 读取字符或控制事件，执行退格反馈、EOF-like/interrupt-like 解释或把输入交给 shell 行缓冲。
- Shell 在用户态完成有界解析、内建执行、fork/exec/wait、pipe/重定向设置和错误恢复；内核仅提供现有 syscall 语义和 deterministic errno/status。
- 子进程 exit、fault 或 signal terminate 进入现有 zombie/reap 生命周期，父进程通过 wait/waitpid 观察有界状态编码。

## Goals / Non-Goals

**Goals:**

- 明确 wait/waitpid 对具体 PID、任意子进程、无子进程、unsupported options、signal/fault termination 的有界行为。
- 让默认终端对换行、退格、EOF-like、interrupt-like 输入提供可观察且非 IRQ 的消费语义。
- 让 shell 在语法错误、命令缺失、exec 失败、pipe/重定向失败、控制输入中断后恢复到下一次 prompt/read loop。
- 保证 pipe/重定向设置失败不会污染父 shell 的 stdin/stdout/stderr 或无关 fd。
- 让验证以构建、静态检查和可用时的 QEMU/Bochs 行为观察覆盖组合路径。

**Non-Goals:**

- 不实现 sessions、terminal process groups、job control、background jobs、termios、伪终端、多终端设备模型或完整 POSIX terminal。
- 不实现完整 POSIX shell grammar、quoting、globbing、变量展开、脚本、pipeline 链、命令替换或 shell functions。
- 不引入动态链接、共享库、完整 POSIX libc、线程、SMP、async I/O、持久完整 writable filesystem 或 broad file-backed `mmap`。
- 不改变 boot protocol、kernel link address、page-table layout、direct map、GDT/TSS、CR3 switching、InterruptFrame、syscall vector `0x80`、disk layout 或用户 ELF ABI。

## Decisions

### Decision: 把 Stage 42 作为现有能力的 delta，而不是新建能力域

选择修改 `process-lifecycle`、`signals`、`minimal-terminal-abstraction`、`user-shell` 和 `posix-like-process-io-subset`，因为 Stage 42 的目标是交互兼容性和组合行为，不是引入新的独立内核子系统。

替代方案是创建 `process-terminal-shell-compatibility` 新 capability。该方案会重复已有 process、terminal、shell 和 bounded POSIX-like 子集的边界，后续 archive 时更容易形成规格分叉。

### Decision: 终端控制输入保持 producer/consumer 分层

Keyboard IRQ1 路径只生成有界输入数据或控制事件，退格可见反馈、EOF-like 处理、interrupt-like cancellation 和 shell policy 都在非 IRQ consumer 或用户态 shell 中完成。

替代方案是在 IRQ handler 中直接执行回显或 shell 控制逻辑。该方案会违反 IRQ-context safety，容易引入分配、阻塞、文件系统或 console 输出重入风险。

### Decision: shell 通过保存/恢复 fd 状态隔离 pipe 和重定向失败

Shell 在设置 redirection 或 pipe 前记录所需 fd 状态，失败时关闭临时 fd 并恢复父 shell 标准 fd；成功时只让目标子命令观察改写后的 fd 映射。

替代方案是直接在父 shell fd table 上原地修改并依赖后续恢复。该方案更简单，但任何中途失败都可能破坏交互 shell 的 stdin/stdout/stderr，影响后续命令和验证。

### Decision: wait/waitpid 继续有界，不引入完整 POSIX options

支持当前能力范围内的等待目标匹配和状态写回；unsupported options MUST 返回 deterministic error，而不是静默忽略成完整 POSIX 行为。

替代方案是提前引入更完整的 `waitpid` options。该方案需要更完整的进程状态、信号停止/继续、进程组和 job-control 语义，不符合 Stage 42 边界。

### Decision: 信号终止状态通过现有生命周期观察

Signal default terminate 继续进入现有 exit/fault-to-reaper 生命周期，并把 signal termination 编码为父进程 wait 可观察的 bounded status。Shell 只解释该 bounded status，不假装支持完整 POSIX wait macros。

替代方案是为 shell 单独维护 signal 事件通道。该方案会绕过现有进程生命周期，增加状态来源不一致和回收竞态风险。

### Decision: EOF-like 输入由终端暴露 read 结果，shell 决定退出策略

默认终端层只把 EOF-like 输入暴露为 deterministic EOF-like read result，不直接结束 shell 或其他 reader。交互式 `/bin/sh` 在 line boundary 收到 EOF-like result 时按 shell policy 退出；非 shell reader、重定向输入或未来其他 consumer 只观察普通 EOF-like read 结果。

替代方案是在 terminal/TTY 层直接结束 shell。该方案会让终端层理解具体 consumer，破坏 terminal producer/consumer 分层，也不利于后续非交互输入或普通用户程序复用 stdin 行为。

### Decision: interrupt-like 输入在 Stage 42 只做行取消，不投递前台子命令信号

在没有 sessions、terminal process groups、job control 或 foreground terminal ownership 的前提下，Stage 42 将 interrupt-like 输入定义为 shell line cancellation：编辑行时清空当前输入并恢复 prompt；子命令运行时不向所谓“前台进程组”广播信号，最多记录为 documented no-op 或由 shell 在 wait 返回后恢复状态。向当前前台子命令投递 bounded signal 留给后续显式前台子进程跟踪或终端/session 模型。

替代方案是立即把 interrupt-like 输入映射到 signal delivery。该方案目标选择不稳定，容易暗示 POSIX terminal-generated signal 和 foreground process group 语义，不符合当前边界。

### Decision: waitpid unsupported options 统一返回 -EINVAL

`waitpid` 的 unsupported options 属于参数错误，统一返回 `-EINVAL`，并且不阻塞、不回收子进程、不写 status storage。合法参数下没有可等待目标、目标不是直接子进程或目标已完全回收时，返回独立的 no-child/no-match 错误，例如现有 errno 体系中的 `-ECHILD` 或等价错误。

替代方案是忽略 unsupported options 或把它们折叠成 no-child/no-match。前者会暗示更完整的 POSIX options 兼容性，后者会混淆参数错误和合法查询无结果，降低 shell/libc/验证路径的可诊断性。

## Risks / Trade-offs

- [Risk] 终端控制字符语义与未来 termios 不完全一致 -> Mitigation: 明确描述为 EOF-like/interrupt-like bounded behavior，不声明 canonical mode 或 termios。
- [Risk] shell fd 恢复路径遗漏临时 fd 或重复 close -> Mitigation: tasks 中要求按成功/失败路径审查 fd ownership、引用计数和父 shell 标准 fd 保持。
- [Risk] wait/waitpid 状态编码和 libc wrapper 宏不一致 -> Mitigation: 先稳定内核 bounded status，再让 libc/shell 只消费已定义字段，unsupported macro 行为显式非目标。
- [Risk] 交互验证依赖图形控制台或键盘输入，headless 环境不可复现 -> Mitigation: 优先使用 source/static/build 检查和可脚本化 userland smoke；人工控制台或 Bochs/QEMU 图形观察不可用时记录 skipped validation 和剩余风险。
- [Risk] 组合行为修改跨 kernel/user 多处，易引入历史诊断噪声 -> Mitigation: validation notes 区分历史诊断、当前 change 引入的问题、clang/clangd freestanding false positives 和工具链不可用。

## Migration Plan

1. 先整理规格和文档边界，确认不扩大 POSIX、terminal 或 shell 承诺。
2. 分子系统实现或修复 wait/waitpid status、terminal control input、shell fd isolation 和 error reporting。
3. 扩展最小用户态验证程序或 shell 可观察路径，覆盖成功组合和失败恢复。
4. 运行窄构建、辅助 clang/clangd 静态检查和可用时的 QEMU/Bochs runtime smoke。
5. 若运行时验证环境不可用，记录缺失工具链、模拟器、ROM/display、disk image 或 oracle，并列出替代检查和剩余风险。

Rollback 策略是按子系统回退：先禁用或撤回 shell/userland 表层改动，再回退 terminal control input 消费逻辑，最后回退内核 wait/status 或 signal-status 调整。不得回退无关本地修改。

## Open Questions

- 当前无未决设计问题。EOF-like、interrupt-like 和 `waitpid` unsupported options 已在 Decisions 中固定为 Stage 42 行为。
