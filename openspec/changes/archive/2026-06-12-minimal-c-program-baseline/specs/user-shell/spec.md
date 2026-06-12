## ADDED Requirements

### Requirement: Shell 执行 基线 C 程序并保留可观察结果

BigOS shell SHALL 能执行 基线小型静态 C 程序，并保留其用户可观察行为：参数传递、stdout/stderr 输出、退出状态和确定性错误报告。该需求 MUST 不扩展为完整 POSIX shell、作业控制、terminal process group 或复杂脚本语义。

#### Scenario: Shell 传递 argv 给外部 C 程序

- **WHEN** 用户或验证输入通过 shell 启动一个 基线 C 程序并携带参数
- **THEN** shell MUST 以 NULL 结尾 `argv` 调用现有外部命令执行路径
- **AND** 被执行程序 MUST 能观察到对应 `argc` 和参数字符串

#### Scenario: Shell 展示程序输出和错误

- **WHEN** 基线 C 程序向 stdout 或 stderr 写入文本
- **THEN** shell 的交互或验证路径 MUST 让这些输出经当前 fd/console/serial 可观察
- **AND** shell MUST NOT 把外部程序的普通错误报告误处理为 shell 自身崩溃

#### Scenario: Shell 在程序结束后继续运行

- **WHEN** 基线 C 程序正常退出或以非零状态退出
- **THEN** shell MUST 回到有界读-解析-执行循环
- **AND** shell MUST 保留外部程序退出状态的可观察性，至少用于验证路径判断成功或失败
