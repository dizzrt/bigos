## ADDED Requirements

### Requirement: TTY input exposes terminal events without widening IRQ work
BigOS SHALL extend the TTY input path so the default terminal can represent printable input and a bounded set of control-character events needed by shell/userland consumers. Keyboard IRQ1 MUST remain an IRQ-safe producer: it MAY classify and enqueue fixed-size input records or characters, but it MUST NOT perform ordinary echo, terminal policy, shell cancellation, process signaling, or dynamic terminal state updates that require non-interrupt context.

#### Scenario: Control input is enqueued as bounded data
- **WHEN** keyboard IRQ1 observes a supported control-key combination for the default terminal
- **THEN** BigOS MUST enqueue a bounded character or input event representation into TTY-owned fixed-capacity state
- **AND** the IRQ handler MUST NOT allocate memory, block, sleep, call `mdelay()`, use filesystem services, call `kprintf`, perform ordinary console output, or depend on user-mode progress

#### Scenario: Terminal policy remains outside IRQ
- **WHEN** EOF-like, interrupt-like, newline, or backspace-like input is later consumed
- **THEN** the non-interrupt terminal consumer or shell path MUST decide the observable result
- **AND** keyboard IRQ1 MUST NOT directly kill processes, rewrite shell state, or emit ordinary prompt/echo output

### Requirement: Console echo and editing feedback use non-interrupt paths
BigOS SHALL keep printable input echo, line-end feedback, and backspace/delete-like visual feedback in non-interrupt terminal, console, or userland consumer paths. The feedback MUST be bounded and deterministic enough for manual validation, while preserving existing early diagnostic output independence.

#### Scenario: Printable echo is outside IRQ
- **WHEN** a printable character reaches the default terminal input buffer from keyboard IRQ1
- **THEN** BigOS MUST NOT echo that character directly from IRQ context
- **AND** any visible echo MUST come from a non-interrupt terminal consumer, shell line-input path, or another documented non-IRQ console path

#### Scenario: Editing feedback remains minimal
- **WHEN** the user enters newline, carriage return, backspace, delete-like input, or unsupported control bytes on the default terminal
- **THEN** BigOS MUST produce deterministic bounded feedback or documented no-op behavior from a non-interrupt path
- **AND** the behavior MUST NOT require terminal escape support, termios, process groups, sessions, dynamic allocation in IRQ context, or full POSIX terminal state
