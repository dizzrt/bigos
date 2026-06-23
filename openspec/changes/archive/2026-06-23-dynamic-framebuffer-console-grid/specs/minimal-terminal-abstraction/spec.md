## ADDED Requirements

### Requirement: 默认控制台 viewport 使用 backend-provided visible grid
BigOS SHALL allow the default runtime console's visible viewport size to come from the selected render backend. The terminal abstraction MUST remain a bounded single-console text display path and MUST NOT expose backend geometry as a new user-visible terminal ABI.

#### Scenario: Framebuffer backend changes visible grid transparently
- **WHEN** the framebuffer render backend reports a dynamic visible grid larger than 80x25
- **THEN** ordinary stdout/stderr, shell prompt text, kernel runtime console output, cursor placement, and scrollback viewport redraw MUST use that grid
- **AND** user programs and fd/syscall consumers MUST NOT need to know the active backend geometry

#### Scenario: Legacy VGA keeps existing behavior
- **WHEN** the selected backend is Legacy VGA text
- **THEN** the default runtime console MUST use the fixed 80x25 visible grid
- **AND** existing Legacy fallback behavior, serial diagnostics, and bounded userland baseline MUST remain available

#### Scenario: Terminal semantics remain minimal
- **WHEN** the default runtime console uses a dynamic framebuffer visible grid
- **THEN** BigOS MUST still treat it as a bounded default console display feature
- **AND** it MUST NOT imply ANSI/VT compatibility, `termios`, multiple terminals, pseudo-terminals, full graphical terminal behavior, dynamic font scaling, locale/shaping, or complete POSIX terminal behavior

#### Scenario: Scrollback ownership remains in console state
- **WHEN** backend-provided visible rows change the amount of text visible on screen
- **THEN** scrollback retention, viewport navigation, clear-screen policy, bottom-follow policy, cursor state, and Unicode cell layout MUST remain owned by runtime console state
- **AND** display backends MUST NOT maintain independent user-visible histories or terminal state
