## Context

Stage 19 之后，BigOS normal boot 已经能打包并启动 resident PID-1 init，再进入 `/bin/sh` 和小型用户程序集合。当前缺口不是新增完整用户态模型，而是把已有键盘 TTY、文本控制台、fd/syscall、init 与 shell 路径整理成默认可见、可操作的交互路径：用户在运行时文本控制台上应能看到 prompt、输入命令、看到输入回显与命令输出。

本 change 横跨内核 terminal/TTY、键盘 IRQ handoff、控制台输出、用户态 shell、init 默认 fd 连接，以及 runtime validation。它必须保持当前 x86_64 Legacy BIOS 后端、单核同步模型、现有磁盘镜像路径和串口/日志验证方式，不改变 boot 地址、链接地址、页表布局、IDT/syscall vector、CR3 切换或用户 ABI。

## Goals / Non-Goals

**Goals:**

- 将默认运行时文本控制台作为 `/bin/sh` 的用户可见交互入口。
- 确保 shell prompt、键盘输入回显、命令输出与错误输出能通过有界 fd/TTY/console 路径显示。
- 保持键盘 IRQ1 只做有界 scancode 转换和 TTY enqueue/wakeup，不在中断上下文直接写普通回显。
- 保留 headless 串口/日志行为断言，使自动化验证不依赖人工键盘输入。
- 记录人工或图形化控制台验证的执行/跳过原因与剩余风险。

**Non-Goals:**

- 不实现完整 POSIX terminal、termios、session、terminal process group 或 job control。
- 不引入 SMP、异步 I/O、动态链接、完整 POSIX libc、持久完整可写文件系统或广泛设备支持。
- 不新增 UEFI、virtio、AHCI/SATA、NVMe 或第二架构运行时等价 backend。
- 不改变 kernel/user ABI、syscall vector、boot/disk layout、链接地址或页表布局。
- 不要求每次 automated smoke 都具备图形显示、手动键盘输入或 emulator scancode injection。

## Decisions

1. 默认交互路径复用现有 TTY/console/fd 模型，而不是新增独立 shell console 后门。

   Rationale: shell 需要验证真实用户态 fd 和 syscall 行为；绕过 fd 直接写控制台会掩盖进程、VFS、dup/pipe/redirection 与 stdout/stderr 行为的缺陷。

   Alternatives considered: 为 `/bin/sh` 增加专用 kernel console syscall。该方案实现更快，但会制造第二套 I/O 语义，削弱 Stage 23/24 的 POSIX-like 进程与 I/O 演进。

2. 输入回显由非中断消费路径负责，而不是由 keyboard ISR 直接写 VGA/serial。

   Rationale: 现有 TTY spec 已要求 ISR 不直接写普通输出。交互控制台可用性继续保持 IRQ handler 有界、allocation-free、non-blocking，只允许 enqueue 和 bounded wakeup。

   Alternatives considered: 在 IRQ1 收到 printable key 时立即写 VGA。该方案降低延迟，但把 UI 输出耦合到硬件中断路径，扩大 port I/O 和显示行为的中断安全风险。

3. prompt 和用户态输出属于 shell/userland 行为，控制台只提供有界字符输入输出承载。

   Rationale: prompt 是 shell 交互协议的一部分，应由 `/bin/sh` 在读取下一行前输出并 flush 到 stdout。控制台层不应知道 shell 状态或命令语义。

   Alternatives considered: 由 PID-1 init 或 kernel TTY 自动打印 prompt。该方案会与 shell 内建、外部命令、重定向和管道语义冲突。

4. 自动化验证优先保留串口/日志行为断言，人工控制台验证作为分层补充。

   Rationale: headless QEMU 更适合 CI-like smoke；真实键盘和 VGA 可见性受本地 emulator/display 配置影响。交互控制台可用性需要证明不破坏 default boot/userland markers，同时记录交互路径的手动证据或跳过原因。

   Alternatives considered: 强制所有验证通过图形 QEMU 或 Bochs 手动输入。该方案不可移植，容易把环境缺失误判为代码失败。

5. shell 交互判定采用最小 `isatty` 风格 wrapper，而不是 init 固定约定或暴露完整 fd 类型模型。

   Rationale: shell 需要知道何时显示 prompt，但 交互控制台可用性不需要完整 POSIX `isatty`。最小 wrapper 只回答 fd 是否绑定到默认 TTY/console，既能避免 prompt 污染重定向/管道输出，也为后续 POSIX-like 用户态演进保留清晰语义。

   Alternatives considered: 复用内部 fd 类型信息会把内核/VFS 细节泄露给用户态；由 init 传入固定交互约定实现更简单，但容易在后续重定向、替代 shell 或非交互启动场景中变脆弱。

6. 交互控制台可用性包含最小 backspace 行编辑，但不扩展为完整 terminal editing。

   Rationale: 只显示 backspace 效果但不修改实际输入缓冲会造成“屏幕看起来删除了字符，shell 实际仍读到旧字符”的可用性陷阱。最小行编辑仅覆盖 printable、newline 和 backspace，保持行长和缓冲边界明确。

   Alternatives considered: 完全不处理 backspace 会让默认交互 shell 难以实际使用；支持方向键、历史记录、escape sequence、termios 或复杂编辑状态会越过 交互控制台可用性的边界。

## Risks / Trade-offs

- [Risk] 文本控制台可见但串口日志不可判断交互是否完整 → Mitigation: 保留默认 init/userland marker 断言，并把人工控制台检查记录为分层验证证据。
- [Risk] 回显路径误入 keyboard ISR，破坏 IRQ-safe 边界 → Mitigation: spec 明确要求回显来自非中断消费路径，tasks 中加入源码检查和 targeted review。
- [Risk] shell prompt 在重定向或管道场景中污染非交互输出 → Mitigation: prompt 仅在交互式 stdin/stdout 绑定到默认 TTY/console 时显示，非交互路径保持有界命令语义。
- [Risk] 控制字符处理不足导致输入体验不稳定 → Mitigation: 交互控制台可用性只要求基本 printable、newline、backspace 的有界行为；复杂 terminal editing 留作后续阶段。
- [Risk] 本地 emulator 缺少键盘/display 支持 → Mitigation: validation artifact 记录图形/人工检查跳过原因、替代 headless 检查和剩余风险。

## Migration Plan

1. 先补齐 TTY/console 与 fd 连接的 source-level 检查，确认默认 init/shell 的 stdin/stdout/stderr 指向有界交互路径。
2. 调整或补齐 shell prompt、flush、readline、回显和错误输出行为，使默认文本控制台可用。
3. 增加/更新 headless 行为断言，确认默认 userland 路径和串口/log marker 不回退。
4. 在可用环境中执行图形或人工控制台 smoke；不可用时记录缺失依赖和剩余风险。
5. 如出现回归，可回退 shell prompt/echo 绑定和默认 TTY 连接改动，同时保留不涉及运行时行为的 OpenSpec 记录。

## Open Questions

- 后续是否已有或需要新增可复用的 emulator keyboard injection；交互控制台可用性不把它作为必需项，自动化保留 headless marker，交互体验用人工或可选 emulator 输入验证。
