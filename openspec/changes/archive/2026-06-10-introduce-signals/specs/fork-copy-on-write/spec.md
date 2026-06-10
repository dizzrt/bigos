## ADDED Requirements

### Requirement: fork 继承信号处置与掩码并清空 pending

BigOS SHALL 在 `fork` 复制进程时，让子进程逐字段继承父进程的每信号处置表与阻塞掩码，并把子进程的 pending 信号集清空，且不改变既有 COW 地址空间复制、引用计数、失败回滚与「父返回子 PID、子返回 0」语义。

#### Scenario: 子进程继承处置表与掩码

- **WHEN** 父进程通过 `fork` 复制出子进程
- **THEN** 子进程的每信号处置表与阻塞掩码 MUST 逐字段等于父进程对应值
- **AND** 该继承 MUST NOT 引入新的分配失败路径

#### Scenario: 子进程 pending 信号集清空

- **WHEN** 父进程通过 `fork` 复制出子进程
- **THEN** 子进程的 pending 信号位图 MUST 为空（不继承父进程未投递的 pending 信号）
- **AND** fork 既有的 COW 复制、引用计数、失败回滚与父子返回值语义 MUST 保持不变
