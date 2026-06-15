## ADDED Requirements

### Requirement: 信号终止与 wait 状态对齐
BigOS SHALL align signal default termination with the bounded process wait status contract so that a parent process or shell can observe signal-caused child termination without introducing process groups, job control, terminal-generated POSIX signals, or complete `siginfo` semantics.

#### Scenario: 默认终止动作发布 waitable 状态
- **WHEN** a supported pending signal with default Terminate disposition is delivered to a child process
- **THEN** BigOS MUST terminate the process through the existing exit/fault-to-reaper lifecycle
- **AND** the resulting waitable status MUST encode that termination was caused by the delivered signal

#### Scenario: SIGKILL 状态不可伪装为正常退出
- **WHEN** a process is terminated by `SIGKILL`
- **THEN** the parent-visible status MUST distinguish the termination from normal `exit`
- **AND** userland status helpers MUST NOT report it as a successful zero exit

#### Scenario: signal 状态不引入 job-control 信号语义
- **WHEN** signal termination status is documented, validated, or consumed by shell
- **THEN** it MUST be described as bounded per-process signal termination
- **AND** it MUST NOT imply terminal process groups, foreground/background jobs, stopped/continued states, or POSIX terminal-generated group signaling

### Requirement: interrupt-like terminal input signal behavior remains explicit
BigOS SHALL keep interrupt-like terminal input behavior explicit and bounded. In Stage 42, such input SHALL be exposed as a deterministic shell line-cancellation event while editing input, and SHALL NOT deliver a signal to a foreground child or foreground process group.

#### Scenario: interrupt-like input cancels shell line editing
- **WHEN** the default terminal receives the configured interrupt-like control input while shell line input is active
- **THEN** BigOS MUST expose a documented line-cancellation event to the shell
- **AND** the behavior MUST be observable through prompt recovery or deterministic output

#### Scenario: interrupt-like input avoids implicit process groups
- **WHEN** interrupt-like input is handled without terminal process groups
- **THEN** BigOS MUST NOT broadcast a signal to an unspecified foreground process group
- **AND** it MUST NOT require sessions, job control, termios, or multi-terminal state to decide the target

#### Scenario: interrupt-like input does not signal running child in Stage 42
- **WHEN** a child command is running and the default terminal receives the configured interrupt-like control input
- **THEN** BigOS MUST NOT infer a POSIX foreground child or process group target for signal delivery
- **AND** shell behavior MUST remain bounded as documented no-op, deferred status handling, or prompt recovery after the existing wait path completes
