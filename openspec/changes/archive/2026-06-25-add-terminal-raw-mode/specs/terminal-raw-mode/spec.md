## ADDED Requirements

### Requirement: 默认终端维护有界输入模式

BigOS SHALL maintain a bounded input mode for the single default console terminal. The supported modes SHALL include canonical mode and raw mode. The mode state MUST be fixed-size, terminal-owned, and initialized to canonical mode during normal terminal initialization.

#### Scenario: 初始化为 canonical

- **WHEN** BigOS initializes the default TTY/console path during normal boot
- **THEN** the default terminal input mode MUST be canonical
- **AND** the initialization MUST NOT allocate unbounded state, require filesystem access, or depend on userland progress

#### Scenario: 查询返回当前模式

- **WHEN** a supported kernel path or user syscall queries the default terminal input mode
- **THEN** BigOS MUST return the current canonical/raw mode state deterministically
- **AND** the query MUST NOT mutate terminal input buffers, foreground process groups, fd state, or console output state

#### Scenario: 设置只接受支持模式

- **WHEN** a user process requests an unsupported terminal mode, unknown version, unknown flag, or malformed mode object
- **THEN** BigOS MUST reject the request with deterministic errno
- **AND** the previous terminal mode MUST remain unchanged

### Requirement: canonical mode 保持默认 shell 输入语义

BigOS SHALL keep canonical mode as the default interactive shell mode. In canonical mode, the existing bounded line-oriented input behavior, ordinary echo/feedback policy, EOF-like behavior, interrupt-like behavior, and console scrollback controls MUST remain available.

#### Scenario: canonical read 等待可交付输入

- **WHEN** a user process reads fd `0` from the default terminal while canonical mode is active
- **THEN** BigOS MUST preserve the existing bounded blocking and character/event consumption behavior for ordinary shell input
- **AND** line end, backspace/delete-like feedback, EOF-like input, and interrupt-like input MUST keep their documented canonical behavior

#### Scenario: canonical Ctrl-C 保持前台组语义

- **WHEN** the default terminal receives interrupt-like input while canonical mode is active and a valid foreground process group exists
- **THEN** BigOS MUST preserve the existing bounded foreground-group interrupt behavior
- **AND** the behavior MUST NOT require complete POSIX job control, background read/write control, or full termios

#### Scenario: canonical scrollback controls remain available

- **WHEN** the user presses supported scrollback navigation keys while canonical mode is active
- **THEN** BigOS MAY consume those events as console viewport controls according to the documented scrollback policy
- **AND** consumed viewport controls MUST NOT leak misleading partial bytes to userland stdin

### Requirement: raw mode 逐字节交付输入

BigOS SHALL make raw mode deliver terminal input to userland with minimal kernel interpretation. Raw mode reads from the default terminal MUST return available input bytes or fixed event-derived bytes without waiting for line end and without ordinary terminal echo.

#### Scenario: printable input 立即可读

- **WHEN** raw mode is active and keyboard input enqueues a printable byte
- **THEN** a user process reading fd `0` MUST be able to receive that byte without waiting for newline
- **AND** BigOS MUST NOT generate ordinary echo for that byte from the terminal line discipline

#### Scenario: Ctrl-C 作为输入而非自动 signal

- **WHEN** raw mode is active and the default terminal receives interrupt-like input
- **THEN** BigOS MUST deliver a deterministic input byte or event-derived byte sequence to userland
- **AND** it MUST NOT automatically signal the foreground process group for that input

#### Scenario: EOF-like input 作为输入而非 empty read

- **WHEN** raw mode is active and the default terminal receives EOF-like input
- **THEN** BigOS MUST deliver a deterministic input byte or event-derived byte to userland
- **AND** it MUST NOT convert that input into a canonical empty-read EOF result

#### Scenario: raw read 不为填满用户缓冲等待

- **WHEN** raw mode is active and a user process reads fd `0` with a buffer larger than currently available input
- **THEN** BigOS MUST return 1 to the requested length worth of currently available bytes once at least one byte is available
- **AND** it MUST NOT wait solely to fill the entire user buffer once at least one byte is available

### Requirement: raw mode 改变导航键归属

BigOS SHALL allow raw mode to hand supported navigation and control-key input to userland rather than consuming it as default console policy. The exact byte sequences MAY be provided directly by this capability or by a later ANSI/VT input capability, but raw mode MUST establish that userland owns these keys while raw mode is active.

#### Scenario: raw mode 不消费用户态导航键为 scrollback

- **WHEN** raw mode is active and the default terminal receives a supported navigation key selected for userland delivery
- **THEN** BigOS MUST deliver the corresponding bounded byte or event-derived sequence to userland
- **AND** it MUST NOT consume that key as console scrollback navigation

#### Scenario: canonical mode 可继续消费 scrollback

- **WHEN** canonical mode is active and the default terminal receives a supported scrollback key
- **THEN** BigOS MAY continue consuming that key as console viewport navigation
- **AND** this canonical behavior MUST NOT determine raw mode behavior

#### Scenario: 序列交付失败不产生 partial input

- **WHEN** raw mode delivery of a fixed sequence cannot fit in the TTY input buffer
- **THEN** BigOS MUST drop the whole sequence or apply another documented all-or-nothing policy
- **AND** userland MUST NOT observe a misleading partial escape sequence for that key

### Requirement: terminal mode 控制接口有权限边界

BigOS SHALL allow terminal mode changes only through a bounded default-terminal control interface. Mode changes MUST be accepted only from a permitted process in the current terminal/session/foreground boundary or from a documented shell recovery path.

#### Scenario: foreground process 设置 raw mode

- **WHEN** a process in the current default terminal foreground group requests raw mode using a valid mode object
- **THEN** BigOS MUST set the default terminal mode to raw
- **AND** subsequent default terminal reads MUST observe raw mode behavior

#### Scenario: 非前台进程设置失败

- **WHEN** a process outside the allowed foreground/session boundary requests a terminal mode change
- **THEN** BigOS MUST reject the request with deterministic errno
- **AND** the previous terminal mode and foreground group binding MUST remain unchanged

#### Scenario: shell 恢复 canonical 有确定性路径

- **WHEN** the shell regains the default terminal foreground group after a foreground command exits or fails
- **THEN** BigOS MUST allow the shell or current session leader to restore canonical mode through a bounded recovery path such as `bigos_tcsetmode`
- **AND** failure to restore MUST be reportable without corrupting fd `0/1/2`, TTY input buffers, or process group state

#### Scenario: shell 恢复路径不能设置 raw

- **WHEN** a shell/session-leader recovery path is used outside the ordinary foreground-group permission rule
- **THEN** BigOS MUST allow only canonical restoration
- **AND** it MUST reject attempts to enter raw mode through that recovery exception with deterministic errno

### Requirement: 生命周期防止 raw mode 泄漏

BigOS SHALL prevent completed foreground programs from leaving the default shell permanently trapped in raw mode. Process exit, reap, foreground-group invalidation, and shell foreground restoration MUST have deterministic mode behavior.

#### Scenario: foreground group 消失后可恢复 canonical

- **WHEN** the process group that set raw mode exits completely and is reaped or invalidated as the terminal foreground group
- **THEN** BigOS MUST restore canonical mode automatically or make shell restoration of canonical mode deterministic
- **AND** terminal state MUST NOT retain dangling process references

#### Scenario: exec 不隐式恢复 mode

- **WHEN** a foreground process successfully executes a new image through `execve`
- **THEN** BigOS MUST preserve the current default terminal mode across the image replacement
- **AND** this preservation MUST NOT create a new terminal, duplicate terminal state, or bypass mode permission checks for later changes

#### Scenario: fork 不创建独立 terminal mode

- **WHEN** a process forks while the default terminal is in canonical or raw mode
- **THEN** the child MUST observe the same default terminal mode state as the parent
- **AND** fork MUST NOT copy a private terminal mode object that diverges from the default terminal

### Requirement: terminal raw mode 验证可复现

BigOS SHALL provide deterministic validation for terminal raw mode behavior through source-level checks, userland smoke, and emulator evidence when available.

#### Scenario: source checks 覆盖模式边界

- **WHEN** terminal raw mode is implemented
- **THEN** source-level checks MUST cover canonical default initialization, get/set mode ABI, invalid flag rejection, raw Ctrl-C behavior, raw EOF-like behavior, and canonical preservation
- **AND** checks MUST distinguish current-change failures from historical diagnostics

#### Scenario: runtime smoke 覆盖 raw read

- **WHEN** runtime validation is available with the required toolchain and emulator environment
- **THEN** a bounded userland scenario MUST demonstrate switching to raw mode, reading at least one byte without line end, restoring canonical mode, and continuing shell/default userland progress
- **AND** unavailable QEMU, Bochs, display/ROM, disk image, serial oracle, or timeout dependencies MUST be recorded as skipped validation with residual risk
