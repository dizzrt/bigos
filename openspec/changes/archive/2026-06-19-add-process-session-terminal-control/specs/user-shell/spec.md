## ADDED Requirements

### Requirement: Shell 使用有界 foreground process group 运行前台命令

BigOS shell SHALL run supported external commands and single-pipe commands through bounded foreground process group semantics. The shell MUST place participating child processes in an appropriate process group, bind the default terminal foreground group to that process group while the command is running, wait for eligible children, and restore the shell process group afterward. This MUST NOT introduce background jobs, `fg`/`bg`, job table management, complete POSIX shell grammar, or full terminal control.

#### Scenario: 外部命令成为前台组

- **WHEN** 用户在默认交互式 shell 中运行一个支持的外部命令
- **THEN** shell MUST arrange the child command into a bounded foreground process group
- **AND** 默认终端 foreground group MUST point to that group while shell waits for command completion

#### Scenario: 单级 pipe 共享前台组

- **WHEN** 用户运行支持的 `cmd1 | cmd2` 单级 pipe
- **THEN** shell MUST arrange both participating child processes into the bounded foreground process group for that pipeline
- **AND** shell MUST wait for eligible children and preserve existing pipe endpoint close/status behavior

#### Scenario: 命令结束后恢复 shell 前台绑定

- **WHEN** foreground command 或 single-pipe command completes, fails, or setup fails before launch
- **THEN** shell MUST attempt to restore the default terminal foreground group to the shell process group
- **AND** shell MUST keep stdin/stdout/stderr usable and return to the bounded read-parse-execute loop

#### Scenario: unsupported job syntax 仍被拒绝

- **WHEN** 用户输入 background job、`fg`/`bg` job-control syntax 或超出 bounded shell grammar 的 terminal-control form
- **THEN** shell MUST report deterministic unsupported behavior or command-not-supported result
- **AND** shell MUST NOT create hidden background jobs or claim complete POSIX job control
