## ADDED Requirements

### Requirement: fork 子进程继承父进程身份字段

BigOS SHALL 在 `fork` 复制进程时，让子进程逐字段继承父进程的身份四元组 uid/gid/euid/egid，不改变既有 COW 地址空间复制、引用计数与失败回滚语义。

#### Scenario: 子进程身份等于父进程

- **WHEN** 父进程通过 `fork_current` 复制出子进程
- **THEN** 子进程的 uid/gid/euid/egid MUST 逐字段等于父进程对应值

#### Scenario: 身份继承不影响既有复制语义

- **WHEN** `fork` 在复制身份字段的同时执行 COW 地址空间复制、fd 表复制与引用计数
- **THEN** 既有 COW 复制、用户物理帧引用计数与确定性失败回滚语义 MUST 保持不变
- **AND** 身份字段复制 MUST NOT 引入额外的内存分配失败点或改变父进程返回子 PID、子进程返回 0 的约定
