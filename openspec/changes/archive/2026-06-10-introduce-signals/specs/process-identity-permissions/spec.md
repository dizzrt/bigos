## ADDED Requirements

### Requirement: may_signal 作为 kill 的强制点

BigOS SHALL 把现有纯判定原语 `bigos::cred::may_signal(actor, target)` 接成 `SYS_KILL` 信号投递的实际权限强制点，其判定语义保持不变（root 放行、否则身份匹配、非法输入拒绝），仅新增被实际调用的接线点。

#### Scenario: kill 强制点调用 may_signal

- **WHEN** 一个进程通过 `SYS_KILL` 向目标进程投递信号
- **THEN** 内核 MUST 调用 `may_signal(actor, target)` 判定权限，判定拒绝时 MUST NOT 投递并返回确定性 `-EPERM`
- **AND** `may_signal` 的判定逻辑（root 放行、euid 与目标 uid/euid 匹配放行、空指针/非法输入拒绝）MUST 保持不变

#### Scenario: root 可向任意目标投递

- **WHEN** 投递方进程 euid 为 0（root），对任意存在的目标进程投递合法信号
- **THEN** 权限判定 MUST 放行，内核 MUST 在目标 pending 位图置位该信号

#### Scenario: 非匹配身份投递被拒绝

- **WHEN** 投递方 euid 非 0 且与目标进程身份不匹配
- **THEN** 权限判定 MUST 拒绝，内核 MUST 返回 `-EPERM` 且不修改目标 pending 位图
