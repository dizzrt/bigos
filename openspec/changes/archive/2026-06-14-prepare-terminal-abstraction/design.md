## Context

BigOS 当前默认用户态已经能通过 resident init 进入 `/bin/sh`，并具备键盘 TTY 输入、文本控制台输出、fd/VFS I/O、pipe/dup、signals 和有界 shell 组合能力。Stage 37 的目标不是扩大为完整 POSIX terminal，而是在现有单核、有界用户态、Legacy BIOS baseline 上把“默认控制台终端”整理成可被 kernel TTY、shell、简单用户程序和验证路径共同消费的最小抽象。

当前相关路径跨越 keyboard IRQ1 producer、TTY 输入缓冲、非中断阻塞/非阻塞 consumer、console 输出、用户态 stdin/stdout/stderr 和 shell 行编辑/错误展示。该 change 需要先明确归属边界：IRQ 只负责有界输入生产，终端抽象负责描述输入/输出语义，shell 和用户程序只通过既有 fd/syscall 路径观察普通终端行为。

本设计不改变 boot 地址、linker 地址、page-table layout、direct map、IDT vector、syscall vector `0x80`、GDT/TSS、CR3 switching、disk layout 或 boot handoff ABI。

## Goals / Non-Goals

**Goals:**

- 定义默认控制台终端的最小抽象，包括输入归属、输出归属、控制字符语义和有界状态。
- 保持 keyboard IRQ1 handler 的 bounded/IRQ-safe 属性，将回显、控制字符解释和 shell 策略放在非中断路径。
- 让 `/bin/sh` 和简单用户程序通过 stdin/stdout/stderr 观察终端行为，避免普通路径依赖 early diagnostic-only 输出。
- 为行为导向验证建立分层策略：headless 串口/日志检查保持可自动化，图形/手工输入证据作为环境可用时的补充。

**Non-Goals:**

- 不实现完整 POSIX terminal、termios、terminal process group、session、job control、伪终端或多终端设备模型。
- 不引入 SMP、动态链接、完整 POSIX shell、完整 POSIX libc 或 broad hosted runtime。
- 不改变现有 Legacy BIOS/MBR/exFAT 默认启动/存储路径，也不推进 UEFI runtime parity。
- 不把普通 echo、行编辑或控制字符解释放入 IRQ handler。

## Decisions

1. 最小终端抽象以默认控制台为唯一初始终端对象。

Rationale: 当前 BigOS 只有一个默认文本控制台和一个有界用户态 shell。先定义单一 console terminal 可以为后续交互增强建立契约，而不提前引入 tty device registry、pty、多 session 或 multi-seat 复杂度。

Alternative considered: 立即设计完整 tty device 模型。该方案会把尚未实现的 session/job-control/process-group 假设带入当前阶段，因此排除。

2. 输入数据流保持 producer/consumer 分层。

Rationale: keyboard IRQ1 producer 继续只做 scancode 读取、修饰键状态更新、字符/control event 入队和可选 IRQ-safe wakeup。控制字符解释、回显、EOF/interrupt 结果、shell 行输入反馈在非中断 consumer 或用户态路径完成，避免 IRQ 中分配、阻塞、格式化输出或执行 shell 策略。

Data flow: keyboard IRQ1 -> bounded TTY input/event buffer -> non-interrupt terminal consumer -> fd/syscall stdin -> shell/user program line handling.

Alternative considered: 在 IRQ handler 中直接解释 `Ctrl-C` 或输出回显。该方案违反现有 IRQ-safe 边界，并可能引入 reentrancy、输出锁和调度交互风险。

3. 输出数据流通过既有 fd/syscall/console 路径。

Rationale: 普通 shell prompt、命令输出、错误信息和简单用户程序 stdout/stderr 应通过现有 fd/VFS/syscall 到 console output，而不是调用 early `kput()`/panic marker 路径。early diagnostics 仍保留独立直接输出，以避免 panic 或早期 fault 依赖 TTY 初始化。

Data flow: user stdout/stderr -> syscall write -> fd/VFS terminal sink -> console output API -> VGA text console.

Alternative considered: 让 shell 或用户程序绕过 fd 层直接调用 console。该方案会破坏用户态边界，并降低对 fd 继承、重定向和 pipe 组合行为的验证价值。

4. 控制字符语义采用有界子集。

Rationale: Stage 37 只需要 shell 和简单用户程序可观察的基本控制字符语义。最小集合覆盖 newline/carriage return/backspace、EOF 类输入、interrupt 类输入和 unsupported control 的确定性处理。EOF/interrupt 可被描述为 BigOS bounded terminal event，不承诺 POSIX signal、process group 或 termios 语义。

Alternative considered: 采用 termios/canonical mode 语义。该方案依赖进程组、session 和完整 terminal state，不适合当前成熟度。

5. 验证分层而不强制交互环境。

Rationale: headless QEMU 串口/日志仍是 CI-like 验证首选，图形 QEMU、Bochs、手工键盘输入或 emulator injection 受本地环境影响。验证记录必须区分已通过、跳过/阻塞和残余风险，不能把缺失交互证据记为通过。

Alternative considered: 要求每次运行图形 QEMU 手动验证。该方案不可复现且不适合默认自动化路径。

## Risks / Trade-offs

- [Risk] 控制字符语义过早被误读为 POSIX terminal 兼容 -> Mitigation: specs 和文档明确只承诺 BigOS bounded terminal subset，并列出 termios/session/job-control/process-group non-goals。
- [Risk] IRQ handler 意外承担 echo 或控制字符策略 -> Mitigation: 将相关要求写入 `tty-console-input` delta，并在任务中加入 IRQ safety review 与静态/source 检查。
- [Risk] 用户态输出绕过 fd/syscall 导致 redirection/pipe 语义退化 -> Mitigation: `minimal-terminal-abstraction` 和 `user-shell` 要求普通输出通过 stdin/stdout/stderr 路径观察。
- [Risk] 交互验证依赖本地 display/ROM/keyboard 能力 -> Mitigation: runtime validation delta 要求记录可自动化 headless 检查、可选交互证据、跳过原因和残余风险。
- [Risk] 抽象层引入额外状态后影响 early diagnostics -> Mitigation: early panic、fault、smoke marker 和 direct VGA/COM1 diagnostic path 保持独立，不依赖终端初始化。

## Migration Plan

- 先增加最小终端抽象和控制字符分类，不改变默认 boot/layout/ABI。
- 将现有 TTY input、console output、fd-backed stdin/stdout/stderr 和 shell 行输入逐步接到该抽象边界。
- 保留现有 early diagnostic direct-output 行为和默认-off smoke 语义。
- 若实现中发现终端抽象影响 normal boot、shell 交互或 validation path，可回滚到现有 TTY/console 直接连接方式，同时保留 spec 中的 non-goals 作为边界参考。

## Resolved Decisions

- EOF 类控制输入表现为 read 层可理解的 EOF/empty-read 结果；shell 在空命令行收到 EOF 后退出，不为本阶段新增专用 BigOS terminal event ABI。
- Interrupt 类控制输入在 shell 读行时取消当前输入并回到 prompt；外部命令运行期间先不承诺杀进程、foreground process group 或 POSIX terminal-generated signal 语义。
- Terminal 查询本阶段暂不引入通用 `isatty` wrapper；shell 先通过启动约定和现有 fd 连接状态处理 prompt，只有实现中证明必要时才考虑 BigOS-specific bounded query。
