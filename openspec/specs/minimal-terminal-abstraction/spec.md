## Purpose

Define the minimal default console terminal abstraction boundary for BigOS.
This capability describes bounded ownership of the current single runtime text
console's input, output, and state without introducing full POSIX terminal
semantics.
## Requirements
### Requirement: 默认控制台终端具有最小抽象边界

BigOS SHALL 定义一个默认控制台终端抽象，用于描述当前单一运行时文本控制台的有界输入、输出和状态归属。该抽象 MUST 建立在现有 x86_64 Legacy BIOS baseline、TTY 输入缓冲、console output API 和用户态 fd/syscall 路径之上，MUST NOT 引入多终端设备模型、伪终端、session、job control、terminal process group 或完整 POSIX terminal 支持。

#### Scenario: 默认终端绑定到现有控制台路径

- **WHEN** normal boot 进入默认 resident init 并启动 `/bin/sh`
- **THEN** BigOS MUST 将 shell 的标准输入、标准输出和标准错误连接到默认控制台终端语义
- **AND** 该语义 MUST 通过现有 TTY/console/fd/syscall 路径实现，而不是要求 shell 或用户程序直接访问 VGA、COM1 或 early diagnostic-only API

#### Scenario: 终端抽象不扩大 backend 范围

- **WHEN** 默认控制台终端能力被实现、记录或验证
- **THEN** it MUST NOT require UEFI runtime parity, OVMF, ESP/FAT images, virtio, AHCI/SATA, NVMe, SMP, a second ISA backend, dynamic linking, or a complete hosted runtime
- **AND** it MUST NOT change kernel link addresses, page-table layout, direct-map assumptions, IDT vectors, syscall vector `0x80`, CR3 switching rules, disk layout, or boot handoff ABI

### Requirement: 终端输入归属保持 IRQ producer 与非中断 consumer 分层

BigOS SHALL keep terminal input ownership split between an IRQ-safe keyboard producer and non-interrupt terminal consumers. Keyboard IRQ1 MAY enqueue bounded input characters or terminal input events and MAY wake waiters through a bounded IRQ-safe path, but ordinary echo, line discipline decisions, EOF/interrupt interpretation, and shell policy MUST occur outside IRQ context.

#### Scenario: IRQ 只生产有界输入

- **WHEN** keyboard IRQ1 receives a supported scancode or control-key combination
- **THEN** BigOS MUST translate or classify it into bounded input data or a bounded terminal input event
- **AND** the IRQ handler MUST NOT allocate memory, block, sleep, call `mdelay()`, use filesystem services, call hosted runtime APIs, perform ordinary console echo, or directly execute shell policy

#### Scenario: 非中断路径消费终端输入

- **WHEN** shell or another ordinary non-interrupt consumer reads from the default terminal stdin path
- **THEN** BigOS MUST deliver input in deterministic FIFO or documented event order within fixed bounds
- **AND** empty input MUST either follow the existing non-blocking behavior or block only through the existing non-interrupt wait path

### Requirement: 终端控制字符语义保持有界

BigOS SHALL define a minimal bounded set of terminal control-character semantics needed by the default shell and simple user programs. The set MUST include deterministic handling for line end, backspace/delete-like editing feedback, EOF-like input, interrupt-like input, and unsupported control bytes. These semantics MUST NOT imply termios, canonical mode completeness, signal delivery to process groups, or complete POSIX terminal behavior.

#### Scenario: 行结束和退格可观察

- **WHEN** a user types printable characters, newline/carriage return, or backspace-like input on the default terminal path
- **THEN** the shell or terminal consumer MUST observe deterministic line input and bounded feedback behavior
- **AND** the behavior MUST NOT require terminal escape parsing, dynamic allocation, job-control state, or session state

#### Scenario: EOF-like input is deterministic

- **WHEN** the default terminal receives the configured EOF-like control input
- **THEN** BigOS MUST expose a deterministic EOF-like result to the non-interrupt reader or shell line-input path
- **AND** that result MUST NOT imply complete POSIX canonical-mode semantics

#### Scenario: Interrupt-like input is bounded

- **WHEN** the default terminal receives the configured interrupt-like control input
- **THEN** BigOS MUST expose a deterministic bounded result such as a terminal event, shell-visible cancellation, or documented no-op
- **AND** the behavior MUST NOT require terminal process groups, job control, sessions, or full POSIX signal terminal semantics

### Requirement: 终端输出归属通过普通 fd/syscall 路径

BigOS SHALL route ordinary shell prompt text, command output, shell errors, and simple user-program stdout/stderr through the existing userland fd/syscall path to the default console terminal output sink. Early diagnostics, panic paths, page-fault diagnostics, and fixed smoke markers MAY continue to use direct VGA/COM1 diagnostic APIs independently of terminal initialization.

#### Scenario: 普通输出经 fd 到控制台

- **WHEN** `/bin/sh` or a simple user program writes bounded text to stdout or stderr connected to the default terminal
- **THEN** BigOS MUST make that text visible through the runtime text console output path
- **AND** the program MUST NOT require direct access to VGA memory, COM1 port IO, or kernel diagnostic-only output APIs

#### Scenario: Early diagnostics remain independent

- **WHEN** a panic, early fault, or runtime smoke marker is emitted before or outside terminal readiness
- **THEN** BigOS MAY continue using existing direct diagnostic output paths
- **AND** those diagnostic paths MUST NOT depend on terminal input state, shell progress, or userland fd availability

### Requirement: 默认控制台输出状态支持可替换渲染 backend

BigOS SHALL keep the default runtime console's cell state, cursor position, scrollback history, viewport policy, and ordinary terminal output sink independent from the concrete display backend. The default console MAY render through VGA text or a bounded framebuffer text backend, but ordinary shell stdout/stderr and runtime console output MUST continue to enter through the existing console/terminal path.

#### Scenario: 普通输出不感知显示 backend

- **WHEN** `/bin/sh` or a simple user program writes bounded text to stdout or stderr connected to the default terminal
- **THEN** BigOS MUST route that text through the existing default terminal and runtime console output path
- **AND** backend selection MUST NOT require user programs, shell code, or fd/syscall consumers to know whether VGA text or framebuffer rendering is active

#### Scenario: scrollback 状态由 console 统一拥有

- **WHEN** the selected display backend changes between VGA text fallback and framebuffer text rendering
- **THEN** BigOS MUST keep scrollback retention, viewport navigation, cursor cell state, clear-screen policy, and bottom-follow policy owned by the default runtime console state
- **AND** a display backend MUST NOT maintain an independent user-visible scrollback history that diverges from the default console state

#### Scenario: backend 边界不扩大终端语义

- **WHEN** the default runtime console renders through a framebuffer backend
- **THEN** the default terminal abstraction MUST remain a bounded single-console text display feature
- **AND** it MUST NOT imply ANSI/VT100 compatibility, `termios`, multiple terminals, pseudo-terminals, background read/write control, complete job control, or complete POSIX terminal behavior

### Requirement: 默认控制台终端支持有界 foreground process group binding

BigOS SHALL extend the default console terminal abstraction with a single bounded foreground process group binding. The binding MUST live within the existing default terminal path and MUST NOT introduce multiple terminals, pseudo-terminals, `termios`, background read/write control, or complete POSIX terminal behavior.

#### Scenario: 默认终端记录前台组

- **WHEN** shell 或支持的用户程序设置一个有效 process group 为默认终端 foreground group
- **THEN** 默认终端 MUST 记录该 `pgid`
- **AND** 后续查询和 interrupt-like input targeting MUST 使用该绑定

#### Scenario: 终端绑定不改变输出路径

- **WHEN** 默认终端 foreground group 被查询或设置
- **THEN** 普通 stdout/stderr 输出 MUST 继续通过现有 fd/syscall 到 console output sink 的路径
- **AND** early diagnostics、panic 和 smoke marker MAY 继续独立使用现有 VGA/COM1 diagnostic path

#### Scenario: 无效绑定失败不破坏终端

- **WHEN** 进程请求将无效、不可见或不允许的 group 设置为默认终端 foreground group
- **THEN** 默认终端 MUST 保留旧 foreground group 并返回确定性失败
- **AND** stdin/stdout/stderr、TTY input queue 和 shell prompt feedback MUST 保持可用

### Requirement: 默认控制台输出支持自动上卷

BigOS SHALL make the default runtime text console automatically scroll visible VGA text output when ordinary console text reaches the bottom of the 80x25 display. Automatic scrolling MUST preserve a valid visible cursor position, clear newly exposed cells deterministically, and remain within the existing VGA text-mode hardware assumptions.

#### Scenario: 换行越过最后一行触发上卷

- **WHEN** runtime console output emits a newline while the cursor is on the last visible row
- **THEN** BigOS MUST move existing visible rows up by one row
- **AND** BigOS MUST clear the last visible row with the normal text attribute
- **AND** the cursor MUST remain on the last visible row instead of moving outside the 80x25 visible screen

#### Scenario: 字符输出越过最后一个 cell 触发上卷

- **WHEN** runtime console output writes a printable character at the last visible cell and advances the cursor
- **THEN** BigOS MUST keep the written character visible according to the documented text-mode behavior
- **AND** any next printable character or newline MUST continue from a valid visible cursor position after deterministic scrolling if required

#### Scenario: 自动上卷保持硬件边界稳定

- **WHEN** automatic scrolling occurs on the default x86_64 Legacy BIOS VGA backend
- **THEN** BigOS MUST keep the existing VGA text memory base, port I/O constants, boot handoff, linker addresses, IDT vectors, syscall vector, page-table layout, and disk layout unchanged
- **AND** automatic scrolling MUST NOT require UEFI runtime parity, graphical mode, dynamic linking, hosted runtime services, or a second display backend

### Requirement: 默认控制台维护有界 scrollback 历史

BigOS SHALL maintain a fixed 256-line scrollback history for ordinary runtime console output. The history MUST be owned by kernel console/terminal state, MUST have deterministic overflow behavior, and MUST NOT require dynamic growth, filesystem persistence, or a user-visible terminal ABI.

#### Scenario: 普通输出进入 scrollback 历史

- **WHEN** shell prompt text, user stdout/stderr, or ordinary runtime kernel console text is written through the default console output path
- **THEN** BigOS MUST record the resulting text cells or logical lines in bounded scrollback state
- **AND** the visible screen MUST be renderable from that bounded state

#### Scenario: scrollback 容量耗尽丢弃最旧历史

- **WHEN** console output exceeds the fixed 256-line scrollback capacity
- **THEN** BigOS MUST discard the oldest retained history deterministically
- **AND** BigOS MUST preserve the newest output and a valid visible viewport
- **AND** BigOS MUST NOT allocate additional memory, write history to the filesystem, or corrupt current TTY input state

#### Scenario: 清屏重置可见窗口

- **WHEN** the default runtime console is cleared through the supported console clear path
- **THEN** BigOS MUST clear the visible VGA text window and reset the scrollback viewport to the current bottom
- **AND** the behavior MUST be deterministic whether retained historical rows are preserved or explicitly discarded by the documented clear policy

### Requirement: scrollback 视口可有界重绘

BigOS SHALL allow the default console to render a bounded viewport over retained scrollback history. Viewport changes MUST redraw the visible 80x25 text screen deterministically from console-owned state and MUST keep ordinary output behavior well-defined while the user is viewing history.

#### Scenario: 翻到历史视口后重绘屏幕

- **WHEN** a non-interrupt terminal consumer adjusts the console viewport away from the newest output
- **THEN** BigOS MUST redraw the visible 80x25 VGA text window from retained scrollback history
- **AND** the redraw MUST NOT require shell cooperation, userland direct VGA access, filesystem services, or dynamic allocation

#### Scenario: 底部视口跟随新输出

- **WHEN** the current viewport is at the newest output and additional runtime console text is written
- **THEN** BigOS MUST keep the visible viewport following the newest output
- **AND** automatic scrolling and cursor placement MUST remain deterministic

#### Scenario: 历史视口收到新输出保持可恢复

- **WHEN** the current viewport is showing retained history and additional runtime console text is written
- **THEN** BigOS MUST preserve a deterministic viewport policy that does not corrupt retained history
- **AND** End MUST allow the user to return to the newest output bottom
- **AND** PageDown MUST move the viewport toward the newest output by a bounded page

### Requirement: scrollback 不扩大默认终端语义

BigOS SHALL treat console scrollback and backend rendering as bounded default terminal display features only. They MUST NOT imply ANSI/VT100 terminal compatibility, `termios`, multiple terminals, pseudo-terminals, background read/write control, complete job control, broad POSIX terminal behavior, a new user-visible device/API contract, or complete graphical terminal behavior beyond the explicitly bounded framebuffer text backend.

#### Scenario: 文档和 headers 保持边界

- **WHEN** documentation, OpenSpec artifacts, headers, tests, or validation notes describe console scrollback or framebuffer console rendering
- **THEN** they MUST describe it as a bounded default runtime text-console feature
- **AND** they MUST NOT claim complete terminal emulation, full POSIX terminal semantics, persistent/unbounded output history, full graphical console support, UTF-8 decoding, CJK display, or Unicode cell layout

#### Scenario: early diagnostics 保持独立

- **WHEN** early panic, page fault diagnostics, memory self-test markers, or fixed COM1 serial validation markers are emitted before or outside terminal readiness
- **THEN** those paths MAY continue using existing direct VGA/COM1 diagnostic output APIs
- **AND** those diagnostic paths MUST NOT depend on scrollback initialization, framebuffer console initialization, keyboard input, shell progress, or userland fd availability
