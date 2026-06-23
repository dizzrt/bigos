## ADDED Requirements

### Requirement: 默认控制台输出支持有界 Unicode 文本显示
BigOS SHALL extend the default runtime console output state so ordinary terminal output can display bounded Unicode text through the selected render backend. This extension MUST remain a single default runtime text-console display feature and MUST NOT widen the terminal abstraction into a complete POSIX or ANSI/VT terminal.

#### Scenario: 普通 stdout/stderr 可进入 Unicode console sink
- **WHEN** `/bin/sh` or a simple user program writes bounded UTF-8 text to stdout or stderr connected to the default terminal
- **THEN** BigOS MUST route that byte stream through the existing default terminal and runtime console output path
- **AND** the path MUST decode and store visible text according to the bounded Unicode console text model

#### Scenario: backend 选择对用户程序透明
- **WHEN** ordinary userland or kernel runtime output is rendered through VGA text fallback or framebuffer text backend
- **THEN** user programs, shell code, fd/syscall consumers, and TTY input producers MUST NOT need to know which display backend is active
- **AND** backend-specific Unicode display or degradation MUST remain behind the runtime console render boundary

#### Scenario: 最小终端语义不扩大
- **WHEN** the default runtime console supports UTF-8 decoding, codepoint cells, and double-width cell handling
- **THEN** the default terminal abstraction MUST still remain a bounded single-console text display path
- **AND** it MUST NOT imply ANSI/VT100 compatibility, `termios`, locale, multiple terminals, pseudo-terminals, background read/write control, complete job control, complete POSIX terminal behavior, Unicode normalization, grapheme cluster handling, input method support, or a complete libc wide-character API

#### Scenario: scrollback 继续由 console 统一拥有
- **WHEN** Unicode text output enters the default runtime console and the user navigates retained history
- **THEN** scrollback retention, viewport navigation, cursor cell state, clear-screen policy, bottom-follow policy, and Unicode cell layout MUST remain owned by the default runtime console state
- **AND** display backends MUST NOT maintain independent user-visible Unicode histories that diverge from the default console state
