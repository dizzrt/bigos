## ADDED Requirements

### Requirement: 默认终端支持有界输入模式抽象

BigOS SHALL extend the default console terminal abstraction with a bounded input mode state that distinguishes canonical input handling from raw input delivery. This extension MUST remain a BigOS-specific single-default-terminal feature and MUST NOT imply complete POSIX `termios`, multiple terminals, pseudo-terminals, background read/write control, or complete job control.

#### Scenario: 模式状态属于默认终端

- **WHEN** the default console terminal is initialized
- **THEN** BigOS MUST own the canonical/raw mode state inside the default terminal abstraction
- **AND** user processes MUST NOT receive independent private terminal mode objects for the same default terminal

#### Scenario: raw mode 不扩大终端设备模型

- **WHEN** documentation, specs, headers, or validation notes describe raw mode
- **THEN** they MUST describe it as a bounded input interpretation mode for the single default console terminal
- **AND** they MUST NOT claim complete POSIX terminal semantics, full `termios`, pseudo-terminal support, multiple TTYs, or background job-control behavior

#### Scenario: 显示 backend 不拥有 mode

- **WHEN** the selected console render backend is VGA text or framebuffer text
- **THEN** terminal input mode MUST remain owned by the default terminal/TTY state
- **AND** display backends MUST NOT maintain independent canonical/raw mode state
