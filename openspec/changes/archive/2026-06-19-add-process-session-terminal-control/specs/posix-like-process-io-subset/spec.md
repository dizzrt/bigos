## ADDED Requirements

### Requirement: 有界 process group 和 foreground terminal control 纳入 POSIX-like 子集

BigOS SHALL 将 process group、session 查询与默认终端 foreground process group binding 纳入当前有界 POSIX-like process/I/O 子集。该子集 MUST 只覆盖单默认终端、foreground command、foreground pipe、有限归属查询/设置和确定性 errno，MUST NOT 声明完整 POSIX job control、后台作业、`termios`、多终端、完整 shell 语言或完整 POSIX process model。

#### Scenario: 子集文档描述新增边界

- **WHEN** BigOS documentation、OpenSpec artifacts、headers 或 validation notes 描述 process group、session 或 foreground terminal behavior
- **THEN** 它们 MUST 将该行为描述为 bounded BigOS POSIX-like subset
- **AND** MUST 同时标注完整 job control、后台作业、`termios`、多终端和完整 POSIX shell 仍不属于支持范围

#### Scenario: 简单程序依赖有界 wrapper

- **WHEN** 简单静态 C 程序调用支持的 `pid/pgid/sid` 或 foreground terminal wrapper
- **THEN** wrapper MUST 通过当前 syscall/errno 约定返回成功值或确定性失败
- **AND** 程序 MUST NOT 需要 hosted runtime、动态链接、完整 libc 或完整 POSIX terminal API

#### Scenario: unsupported job-control form 失败可见

- **WHEN** 用户或程序请求超出 bounded subset 的后台作业、完整 `tcsetpgrp` 语义、`termios` 控制或多终端行为
- **THEN** BigOS MUST 拒绝、忽略或报告确定性 unsupported behavior
- **AND** 该失败 MUST NOT 被验证或文档重新解释为完整 POSIX 兼容成功
