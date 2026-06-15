## ADDED Requirements

### Requirement: Shell 退出状态与错误恢复
BigOS shell SHALL track a bounded last-command status for builtins, external commands, pipe compositions, redirection failures, exec failures, signal-terminated children, and unsupported syntax while remaining in the interactive read-parse-execute loop whenever the shell process itself is not explicitly exiting.

#### Scenario: 外部命令状态被记录
- **WHEN** the shell runs an external command and the child exits normally
- **THEN** the shell MUST update its bounded last-command status from the child wait status
- **AND** it MUST return to the next prompt/read loop

#### Scenario: signal 终止子命令状态被记录
- **WHEN** an external command is terminated by a supported signal and the shell waits for it
- **THEN** the shell MUST record a bounded nonzero status that distinguishes signal termination from successful exit
- **AND** it MUST remain usable for the next interactive command

#### Scenario: unsupported syntax fails without corrupting shell
- **WHEN** user input contains unsupported syntax outside the documented bounded grammar
- **THEN** the shell MUST print a deterministic error, set a bounded failure status, and return to the read loop
- **AND** it MUST NOT fork a partially parsed command or corrupt shell-owned fd state

### Requirement: Shell pipe 和重定向失败隔离
BigOS shell SHALL isolate parent shell fd state from pipe and redirection setup. Setup failure MUST close temporary descriptors, preserve shell stdin/stdout/stderr, and avoid launching a command with partial unintended fd mappings.

#### Scenario: output redirection open 失败保持 stdout
- **WHEN** output redirection setup fails because the target path is read-only, unsupported, missing parent directory, overlong, or otherwise rejected by the bounded VFS contract
- **THEN** the shell MUST report a deterministic error and preserve its original stdout mapping
- **AND** the target command MUST NOT run with a partially redirected stdout

#### Scenario: pipe setup 失败清理临时 fd
- **WHEN** pipe creation, fork, dup, or close setup fails while preparing a single-level pipe composition
- **THEN** the shell MUST close any temporary pipe endpoints it owns, report a deterministic error, and preserve shell standard descriptors
- **AND** it MUST NOT leave the shell permanently blocked on a pipe endpoint it forgot to close

#### Scenario: child redirection does not leak into parent
- **WHEN** a command with supported redirection or pipe setup completes successfully
- **THEN** only the target child command or pipeline endpoint MUST observe the redirected fd mapping
- **AND** the parent shell MUST observe its original stdin/stdout/stderr mappings after child completion

### Requirement: Shell 消费默认终端控制输入
BigOS shell SHALL consume the default terminal's bounded control-input results for line completion, backspace/delete-like editing, EOF-like input, and interrupt-like input without implementing full POSIX shell grammar, job control, terminal process groups, or termios.

#### Scenario: EOF-like input recovers deterministically
- **WHEN** shell stdin receives the documented EOF-like terminal result
- **THEN** an interactive shell at a line boundary MUST exit through its documented builtin-equivalent policy
- **AND** the behavior MUST be observable through exit status or deterministic output

#### Scenario: interrupt-like input cancels current line deterministically
- **WHEN** shell stdin receives the documented interrupt-like terminal result while editing a line
- **THEN** the shell MUST clear the current input line, set a bounded cancellation status, and recover to the next prompt/read loop
- **AND** it MUST NOT require process groups, sessions, job control, or full terminal control

#### Scenario: interrupt-like input does not kill child command
- **WHEN** shell stdin receives the documented interrupt-like terminal result while the shell is waiting for a child command
- **THEN** the shell MUST NOT assume a POSIX foreground process group or deliver a terminal-generated signal to that child in Stage 42
- **AND** the shell MUST keep behavior bounded as a documented no-op, deferred status handling, or prompt recovery after the child wait path completes
