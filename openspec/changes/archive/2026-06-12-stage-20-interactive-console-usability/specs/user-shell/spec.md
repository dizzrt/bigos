## ADDED Requirements

### Requirement: Shell prompt is visible on the default interactive console
BigOS shell SHALL display a deterministic bounded prompt before waiting for an interactive command line when stdin/stdout are connected to the default TTY/console path.

#### Scenario: Prompt appears before input
- **WHEN** normal boot starts resident init and enters `/bin/sh` with standard descriptors connected to the default interactive console
- **THEN** shell MUST write a visible prompt to stdout before blocking for the next command line
- **AND** the prompt MUST be bounded and deterministic enough for manual validation

#### Scenario: Prompt does not pollute non-interactive execution
- **WHEN** shell stdin or stdout is redirected away from the default interactive TTY/console path
- **THEN** shell MUST avoid treating that path as an interactive prompt session
- **AND** command output and redirection semantics MUST remain bounded and deterministic

### Requirement: Shell command interaction is visible through console stdout and stderr
BigOS shell SHALL make command execution feedback visible on the default text console by routing built-in command output, external command stdout/stderr, and deterministic shell error messages through the existing userland fd/syscall path.

#### Scenario: Built-in command output is visible
- **WHEN** 用户在默认控制台 shell 中输入内建命令 `echo hello`
- **THEN** shell MUST 将内建命令输出写入 stdout
- **AND** 该输出 MUST 能通过默认文本控制台被用户看到

#### Scenario: External command output is visible
- **WHEN** 用户在默认控制台 shell 中运行存在的简单外部命令
- **THEN** shell MUST 通过现有 `fork`/`execve`/`wait` 路径执行该命令
- **AND** 子进程 stdout/stderr 的有界文本输出 MUST 能通过默认文本控制台被用户看到

#### Scenario: Shell error is visible and recoverable
- **WHEN** 用户输入不存在的命令、超出有界输入限制或触发可恢复解析错误
- **THEN** shell MUST 向 stderr 或 stdout 输出确定性错误信息
- **AND** shell MUST 回到下一次 prompt/read 循环而不崩溃
