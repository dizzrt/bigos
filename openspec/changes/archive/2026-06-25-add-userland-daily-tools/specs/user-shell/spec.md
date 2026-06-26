## ADDED Requirements

### Requirement: Shell 日常内建命令
BigOS shell SHALL provide bounded daily-use builtins for `pwd`, `help`, `env`, `clear`, `true`, and `false` in addition to existing builtins. These builtins MUST execute inside the shell process without `fork`/`execve`, MUST support existing stdout redirection where applicable, and MUST NOT introduce full POSIX shell expansion, variables, quoting, globbing, scripting control flow, aliases, or job control.

#### Scenario: pwd 内建输出 shell cwd
- **WHEN** 用户在 shell 中运行 `pwd`
- **THEN** shell MUST call the libc/kernel `getcwd` path in the shell process and write the current cwd to stdout
- **AND** failure MUST be reported deterministically without terminating the shell

#### Scenario: help 内建描述能力边界
- **WHEN** 用户在 shell 中运行 `help`
- **THEN** shell MUST print a bounded summary of supported builtins, external command execution, pipe, redirection, and unsupported shell features
- **AND** the summary MUST NOT claim complete POSIX shell behavior

#### Scenario: env 内建打印环境
- **WHEN** 用户在 shell 中运行 `env`
- **THEN** shell MUST print the current environment entries visible to child `execve` calls
- **AND** it MUST NOT require hosted libc environment mutation APIs

#### Scenario: clear 内建使用 ANSI 清屏
- **WHEN** 用户在 shell 中运行 `clear`
- **THEN** shell MUST emit a supported ANSI/CSI clear-screen and cursor-home sequence to stdout
- **AND** the command MUST remain bounded and deterministic if stdout is redirected

#### Scenario: true false 传播状态
- **WHEN** 用户在 shell 中运行 `true` or `false`
- **THEN** shell MUST complete without external process creation
- **AND** `true` MUST produce success status while `false` MUST produce nonzero status observable through the existing shell status builtin

## REMOVED Requirements

### Requirement: 小型 pwd 用户工具
**Reason**: `pwd` 读取当前 shell cwd，作为内建更符合 shell 交互语义；保留同名默认外部程序会让内建和 `/bin/pwd` 的职责重复。

**Migration**: 默认镜像移除 `/bin/pwd` 外部程序，用户通过 shell 内建 `pwd` 获取当前目录。需要测试 `execve` 继承 cwd 时，可通过其他外部工具的相对路径行为或后续专用 smoke 验证。
