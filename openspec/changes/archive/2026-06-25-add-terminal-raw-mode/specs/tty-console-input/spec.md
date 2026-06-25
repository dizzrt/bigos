## ADDED Requirements

### Requirement: TTY 输入消费受 terminal mode 控制

BigOS SHALL route default terminal input through a mode-aware consumer boundary. Canonical mode SHALL preserve existing bounded shell input semantics; raw mode SHALL minimize kernel interpretation and deliver available bytes or event-derived bytes to userland.

#### Scenario: canonical mode 使用既有控制字符策略

- **WHEN** default terminal fd `0` is read while canonical mode is active
- **THEN** BigOS MUST preserve the existing documented behavior for printable input, line end, backspace/delete-like input, EOF-like input, interrupt-like input, and unsupported controls
- **AND** ordinary echo and feedback MUST remain outside keyboard IRQ context

#### Scenario: raw mode 绕过 canonical 控制策略

- **WHEN** default terminal fd `0` is read while raw mode is active
- **THEN** BigOS MUST deliver available input bytes or event-derived bytes without applying canonical line editing, EOF-like empty-read conversion, or interrupt-like signal targeting
- **AND** raw delivery MUST remain bounded by the fixed TTY input buffer and user buffer length

#### Scenario: mode 切换不清空无关输入

- **WHEN** the terminal input mode changes between canonical and raw
- **THEN** BigOS MUST preserve unread input unless a documented deterministic flush policy is explicitly chosen
- **AND** the mode switch MUST NOT corrupt the TTY ring indices, dropped counter, wait queue, or keyboard decoder state

### Requirement: raw mode 保持 IRQ producer 安全

BigOS SHALL keep keyboard IRQ1 handling bounded and IRQ-context safe regardless of canonical or raw mode. Raw mode MUST NOT move line discipline, echo, signal delivery, shell policy, or bulk terminal state changes into IRQ context.

#### Scenario: IRQ 不读取 mode 后执行复杂策略

- **WHEN** keyboard IRQ1 receives input while raw mode is active
- **THEN** the IRQ handler MAY enqueue bounded characters or fixed-size events
- **AND** it MUST NOT allocate memory, block, sleep, call filesystem services, perform ordinary console output, signal processes, or directly execute shell policy

#### Scenario: raw wakeup 使用既有 bounded wait path

- **WHEN** raw mode input is enqueued and a user process is blocked reading fd `0`
- **THEN** BigOS MAY wake the reader through the existing bounded IRQ-safe wakeup path
- **AND** the wakeup MUST NOT perform a direct context switch or blocking operation from keyboard IRQ1

### Requirement: raw mode 与 scrollback 控制分离

BigOS SHALL make terminal mode determine whether supported navigation keys are consumed by console scrollback policy or delivered to userland. Canonical mode MAY retain current scrollback behavior; raw mode MUST reserve userland ownership for supported navigation keys selected by the terminal input policy.

#### Scenario: canonical scrollback 行为保持

- **WHEN** canonical mode is active and a supported scrollback navigation event is consumed by the non-interrupt terminal consumer
- **THEN** BigOS MAY adjust the console viewport and redraw the visible console
- **AND** userland character reads MUST NOT observe partial bytes for the consumed viewport operation

#### Scenario: raw navigation input 交给用户态

- **WHEN** raw mode is active and keyboard input produces a supported navigation event selected for userland delivery
- **THEN** BigOS MUST expose a documented byte or fixed sequence through fd `0`
- **AND** it MUST NOT adjust the console viewport for that event
