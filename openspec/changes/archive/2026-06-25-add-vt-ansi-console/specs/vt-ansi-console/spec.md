## ADDED Requirements

### Requirement: 输出侧 ANSI/CSI 解析是有界状态机

BigOS SHALL parse the supported ANSI/VT output subset through a console-owned fixed-size state machine before updating runtime console cells. The parser MUST remain freestanding-safe, deterministic across bounded `write` calls, and independent from early diagnostic-only output paths.

#### Scenario: 普通文本仍按 UTF-8 输出

- **WHEN** runtime console output receives bytes that are not part of an escape sequence
- **THEN** BigOS MUST continue decoding them through the bounded UTF-8 console text model
- **AND** visible cells, cursor advance, wrapping, scrollback, and viewport redraw MUST remain deterministic

#### Scenario: Escape 序列可跨 write 保留

- **WHEN** a supported escape or CSI sequence is split across multiple default console writes
- **THEN** BigOS MUST retain only fixed-size parser state needed to complete the sequence
- **AND** completion MUST apply the same visible behavior as if the sequence arrived in one write

#### Scenario: 无效序列确定性恢复

- **WHEN** the parser receives an unknown final byte, too many parameters, an overlong numeric parameter, or an otherwise invalid escape sequence
- **THEN** BigOS MUST reset the parser to the ordinary text state or a documented recovery state
- **AND** subsequent ordinary output MUST NOT be lost, misdecoded, or permanently interpreted as escape data

#### Scenario: Early diagnostics 不依赖 parser

- **WHEN** panic, early fault diagnostics, fixed serial markers, `kput()`, or `kputs()` emit output before or outside runtime console readiness
- **THEN** those paths MAY continue using direct VGA/COM1 diagnostic output
- **AND** they MUST NOT depend on ANSI parser state, TTY input state, userland progress, or framebuffer console readiness

### Requirement: SGR 属性更新 console-owned cells

BigOS SHALL support a bounded SGR subset for runtime console output. Supported SGR sequences MUST update console-owned current attributes, and newly written visible cells MUST store attributes derived from that state.

#### Scenario: Reset 恢复默认属性

- **WHEN** console output receives `ESC [ 0 m` or an equivalent empty SGR reset in the supported subset
- **THEN** BigOS MUST restore the default console foreground and background attributes
- **AND** subsequently written cells MUST use the default attributes until another supported SGR sequence changes them

#### Scenario: 前景色和背景色可见

- **WHEN** console output receives supported SGR foreground or background color parameters including `30-37`, `40-47`, `90-97`, and `100-107`
- **THEN** BigOS MUST update the current console attributes deterministically
- **AND** the framebuffer render backend MUST render subsequent cells using a documented deterministic foreground/background pixel mapping
- **AND** the VGA fallback backend MUST apply a documented deterministic mapping or degradation for colors that cannot be represented exactly

#### Scenario: 不支持的 SGR 参数不破坏输出

- **WHEN** console output receives unsupported SGR parameters
- **THEN** BigOS MUST ignore those unsupported parameters or apply a documented safe fallback
- **AND** unsupported parameters MUST NOT corrupt parser state, existing cells, scrollback history, TTY input, or render backend state

### Requirement: 输出侧支持常用光标和擦除操作

BigOS SHALL support a bounded CSI subset for cursor movement, cursor positioning, screen erasure, line erasure, and cursor save/restore within the current runtime console visible grid.

#### Scenario: 相对光标移动被限制在可见网格内

- **WHEN** console output receives supported `CUU`, `CUD`, `CUF`, or `CUB` sequences
- **THEN** BigOS MUST move the logical console cursor within the bounded visible grid
- **AND** movement MUST clamp or otherwise reject out-of-range targets without writing outside console-owned storage

#### Scenario: 绝对光标定位更新写入位置

- **WHEN** console output receives supported `CUP` or `HVP` sequences
- **THEN** BigOS MUST set the logical cursor position to the requested bounded visible row and column using documented one-based ANSI coordinates
- **AND** subsequent ordinary text MUST be written at that position according to the console-owned cell model

#### Scenario: 擦除屏幕和擦除行只修改 bounded cells

- **WHEN** console output receives supported `ED` or `EL` sequences
- **THEN** BigOS MUST clear only the documented portion of console-owned visible cells or retained current lines
- **AND** clearing MUST preserve valid cursor position, fixed-capacity storage bounds, render backend consistency, and early diagnostic independence

#### Scenario: 保存和恢复光标是固定状态

- **WHEN** console output receives supported save-cursor and restore-cursor sequences
- **THEN** BigOS MUST support both `ESC 7`/`ESC 8` and `CSI s`/`CSI u` compatibility forms
- **AND** BigOS MUST save and restore only fixed-size console-owned cursor row and column state
- **AND** save/restore MUST NOT implicitly save or restore SGR attributes, character sets, scroll regions, or other terminal modes
- **AND** restore MUST clamp to the current visible grid if backend dimensions changed

### Requirement: 输入侧导航键暴露有界 ANSI 序列

BigOS SHALL expose default terminal navigation keys to the foreground user program through deterministic ANSI escape byte sequences on the default console stdin path, while preserving IRQ-safe keyboard producer boundaries. Existing BigOS-specific canonical/raw terminal mode SHALL be the mode boundary for how foreground programs consume these bytes; this capability MUST NOT introduce POSIX `termios`. `Shift+PageUp` and `Shift+PageDown` SHALL remain kernel console scrollback shortcuts and MUST NOT be delivered as ordinary stdin bytes.

#### Scenario: 方向键产生用户态可读序列

- **WHEN** keyboard input receives supported arrow-key make events
- **THEN** BigOS MUST enqueue the corresponding fixed ANSI escape byte sequence for later `read(fd=0)` consumption
- **AND** the keyboard IRQ handler MUST NOT allocate memory, block, sleep, perform ordinary console output, or depend on shell progress

#### Scenario: Home End Delete Page keys 有确定性序列

- **WHEN** keyboard input receives supported Home, End, Delete-like, PageUp, or PageDown navigation events
- **THEN** BigOS MUST expose documented fixed escape byte sequences to userland
- **AND** the sequence policy MUST remain deterministic across VGA and framebuffer backends

#### Scenario: 默认导航键不被 scrollback 消费

- **WHEN** PageUp, PageDown, Home, End, Delete, or arrow keys are pressed on the default console stdin path
- **THEN** BigOS MUST deliver the documented ANSI escape byte sequence to the foreground user program rather than consuming the key as an implicit console scrollback operation
- **AND** any future kernel scrollback shortcut MUST use a separately documented key combination or mode policy that does not leak partial escape bytes into userland stdin

#### Scenario: Shift Page keys control kernel scrollback

- **WHEN** keyboard input receives `Shift+PageUp` or `Shift+PageDown` on the default console
- **THEN** BigOS MUST consume the key combination as a bounded kernel console scrollback control
- **AND** userland `read(fd=0)` MUST NOT observe the PageUp/PageDown ANSI sequence or any partial escape bytes for that consumed scrollback shortcut
- **AND** the keyboard IRQ handler MUST only perform bounded modifier/scancode classification and fixed-capacity event enqueue or equivalent IRQ-safe handoff

#### Scenario: 序列入队失败有确定性策略

- **WHEN** the fixed-capacity TTY input buffer lacks space for a complete navigation escape sequence
- **THEN** BigOS MUST apply a documented policy to drop the whole sequence or otherwise avoid delivering a misleading partial sequence
- **AND** the overflow path MUST remain allocation-free and IRQ-safe

#### Scenario: Raw mode exposes sequences without canonical side effects

- **WHEN** the default terminal is in BigOS raw mode and a foreground user program reads from fd `0`
- **THEN** navigation-key escape sequences MUST be delivered as ordinary input bytes without canonical line editing, signal delivery, or console viewport side effects
- **AND** this behavior MUST preserve the existing BigOS-specific raw mode ABI rather than claiming POSIX `termios`

### Requirement: ANSI/VT 支持边界被明确记录

BigOS SHALL describe the runtime console as supporting a bounded ANSI/VT subset rather than a complete POSIX or xterm-compatible terminal.

#### Scenario: 文档不声明完整终端

- **WHEN** docs, specs, validation notes, source comments, or user-facing capability summaries describe ANSI/VT support
- **THEN** they MUST identify the supported bounded output and input subset
- **AND** they MUST NOT claim complete xterm compatibility, complete VT100/VT220 compatibility, POSIX `termios`, pseudo-terminals, multiple TTYs, full shell line editing, mouse support, or full POSIX terminal behavior

#### Scenario: 支持矩阵可验证

- **WHEN** implementation of this change is completed
- **THEN** the repository MUST contain deterministic source-level or runtime validation evidence for the supported parser, SGR, cursor, erase, and input-sequence behaviors
- **AND** validation notes MUST separately record graphical checks that passed, checks that could not run, missing emulator/toolchain dependencies, and remaining terminal-compatibility risk
