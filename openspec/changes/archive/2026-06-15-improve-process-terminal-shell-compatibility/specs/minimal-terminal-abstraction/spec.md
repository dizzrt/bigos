## ADDED Requirements

### Requirement: 默认终端行输入控制语义
BigOS SHALL provide bounded line-input control semantics for the default console terminal so ordinary shell input can handle newline, carriage return, backspace/delete-like editing, EOF-like input, interrupt-like input, and unsupported control bytes through a non-interrupt consumer path.

#### Scenario: newline 完成一行输入
- **WHEN** the default terminal receives printable characters followed by newline or carriage return
- **THEN** the non-interrupt terminal consumer MUST expose a completed bounded input line or equivalent byte stream to the shell stdin path
- **AND** the result MUST preserve deterministic ordering and bounded buffer behavior

#### Scenario: backspace 更新有界行状态
- **WHEN** the default terminal receives a configured backspace/delete-like input while a line buffer contains at least one character
- **THEN** the non-interrupt consumer MUST remove or mark removal of one character according to the documented bounded policy
- **AND** any visible feedback MUST occur outside IRQ context through the ordinary terminal output path

#### Scenario: EOF-like input 可观察
- **WHEN** the default terminal receives the configured EOF-like input at a line boundary or under the documented line state
- **THEN** BigOS MUST expose a deterministic EOF-like result to the shell or reader
- **AND** the terminal layer MUST NOT directly terminate the shell or any other reader
- **AND** the behavior MUST NOT require complete POSIX canonical mode or termios

#### Scenario: unsupported control bytes are safe
- **WHEN** the default terminal receives an unsupported control byte or unsupported key event
- **THEN** BigOS MUST ignore it or expose a documented bounded event
- **AND** it MUST NOT corrupt the input buffer, panic, allocate memory in IRQ context, or execute shell policy from IRQ context

### Requirement: 终端控制输入验证可记录
BigOS SHALL make terminal control-input behavior reviewable through source checks, build checks, and runtime or manual validation records when emulator and console input support are available.

#### Scenario: source checks preserve IRQ boundary
- **WHEN** terminal control input handling is implemented or changed
- **THEN** validation MUST confirm the keyboard IRQ producer does not perform ordinary echo, shell policy, filesystem operations, dynamic allocation, blocking waits, or hosted runtime calls

#### Scenario: runtime validation observes interactive recovery
- **WHEN** QEMU or Bochs console input is available with the required image, display, and toolchain setup
- **THEN** validation MUST observe at least one newline, backspace/delete-like, EOF-like, or interrupt-like behavior through shell prompt recovery or deterministic output
- **AND** if runtime interaction cannot run, the validation record MUST state the missing dependency and remaining terminal-control risk
