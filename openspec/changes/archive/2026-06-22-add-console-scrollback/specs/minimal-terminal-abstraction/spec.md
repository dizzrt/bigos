## ADDED Requirements

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

BigOS SHALL treat console scrollback as a bounded default terminal display feature only. It MUST NOT imply ANSI/VT100 terminal compatibility, `termios`, multiple terminals, pseudo-terminals, background read/write control, complete job control, broad POSIX terminal behavior, or a new user-visible device/API contract.

#### Scenario: 文档和 headers 保持边界

- **WHEN** documentation, OpenSpec artifacts, headers, tests, or validation notes describe console scrollback
- **THEN** they MUST describe it as a bounded default runtime text-console feature
- **AND** they MUST NOT claim complete terminal emulation, full POSIX terminal semantics, graphical console support, or persistent/unbounded output history

#### Scenario: early diagnostics 保持独立

- **WHEN** early panic, page fault diagnostics, memory self-test markers, or fixed COM1 serial validation markers are emitted before or outside terminal readiness
- **THEN** those paths MAY continue using existing direct VGA/COM1 diagnostic output APIs
- **AND** those diagnostic paths MUST NOT depend on scrollback initialization, keyboard input, shell progress, or userland fd availability
