## ADDED Requirements

### Requirement: 进程生命周期维护 process group 和 session 状态

BigOS SHALL 在进程创建、镜像替换、退出、等待和 reap 生命周期中维护 `pgid` 与 `sid` 状态。该状态 MUST 与现有 PID registry、wait/reap 和安全 teardown 规则一致，MUST NOT 引入命名空间、完整 POSIX 权限、SMP 迁移或完整 job-control 生命周期。

#### Scenario: 创建和 fork 初始化归属

- **WHEN** 内核创建初始用户进程或用户进程通过 `fork` 创建子进程
- **THEN** 新进程 MUST 获得确定性的 `pgid` 与 `sid`
- **AND** `fork` 子进程 MUST 继承父进程当前归属，除非后续通过支持的控制接口显式变更

#### Scenario: exec 不重置归属

- **WHEN** 进程通过 `execve` 成功替换镜像
- **THEN** 该进程的 `pid`、`pgid` 与 `sid` MUST 保持不变
- **AND** 新镜像 MUST 可通过支持的查询 wrapper 观察同一归属

#### Scenario: teardown 不留下悬垂 foreground 引用

- **WHEN** process group 中的进程退出并完成 wait/reap
- **THEN** 进程 registry 和默认终端 foreground binding MUST 不再依赖已释放进程对象
- **AND** 后续查询、设置或 shell 恢复 MUST 返回确定性结果而不是访问释放内存
