## ADDED Requirements

### Requirement: Terminal interrupt-like input targets foreground process group

BigOS SHALL allow the default terminal's interrupt-like input to target the current foreground process group through the bounded signal subsystem or an equivalent deterministic terminal event path. The behavior MUST remain within the existing single-core signal delivery model and MUST NOT introduce `SIGSTOP`/`SIGCONT` job control, realtime signals, complete POSIX terminal signal semantics, or SMP cross-core delivery.

#### Scenario: 前台组收到 interrupt-like signal result

- **WHEN** the default terminal receives interrupt-like input while a valid foreground process group is bound
- **THEN** each eligible user process in that foreground group MUST observe the bounded interrupt result according to the signal or terminal-event contract
- **AND** processes outside the foreground group MUST NOT receive that terminal-originated interrupt result merely because they share the same session

#### Scenario: 投递保持非中断安全边界

- **WHEN** keyboard IRQ produces an interrupt-like terminal input event
- **THEN** IRQ context MUST only perform bounded IRQ-safe production or wakeup work
- **AND** process-group traversal, shell policy, blocking operations, filesystem I/O and dynamic allocation MUST occur only outside IRQ context or be explicitly proven unnecessary

#### Scenario: 无 foreground group 时结果确定

- **WHEN** interrupt-like input is consumed while no valid foreground group can be targeted
- **THEN** BigOS MUST ignore it or report a deterministic bounded result
- **AND** the signal subsystem MUST NOT corrupt pending masks, process lifecycle state, or terminal binding
