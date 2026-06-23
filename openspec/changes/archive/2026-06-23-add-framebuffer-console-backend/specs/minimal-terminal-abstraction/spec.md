## ADDED Requirements

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

## MODIFIED Requirements

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
