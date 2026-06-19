## ADDED Requirements

### Requirement: 默认控制台终端支持有界 foreground process group binding

BigOS SHALL extend the default console terminal abstraction with a single bounded foreground process group binding. The binding MUST live within the existing default terminal path and MUST NOT introduce multiple terminals, pseudo-terminals, `termios`, background read/write control, or complete POSIX terminal behavior.

#### Scenario: 默认终端记录前台组

- **WHEN** shell 或支持的用户程序设置一个有效 process group 为默认终端 foreground group
- **THEN** 默认终端 MUST 记录该 `pgid`
- **AND** 后续查询和 interrupt-like input targeting MUST 使用该绑定

#### Scenario: 终端绑定不改变输出路径

- **WHEN** 默认终端 foreground group 被查询或设置
- **THEN** 普通 stdout/stderr 输出 MUST 继续通过现有 fd/syscall 到 console output sink 的路径
- **AND** early diagnostics、panic 和 smoke marker MAY 继续独立使用现有 VGA/COM1 diagnostic path

#### Scenario: 无效绑定失败不破坏终端

- **WHEN** 进程请求将无效、不可见或不允许的 group 设置为默认终端 foreground group
- **THEN** 默认终端 MUST 保留旧 foreground group 并返回确定性失败
- **AND** stdin/stdout/stderr、TTY input queue 和 shell prompt feedback MUST 保持可用
