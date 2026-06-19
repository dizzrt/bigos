# process-session-terminal-control Specification

## Purpose
TBD - created by archiving change add-process-session-terminal-control. Update Purpose after archive.
## Requirements
### Requirement: 进程携带有界 process group 与 session 归属

BigOS SHALL 为每个用户进程维护有界 `pid`、`pgid` 与 `sid` 归属。`pgid` MUST 标识进程所属 process group，`sid` MUST 标识所属 session，初始用户态进程和 shell MUST 获得确定性初始归属。该模型 MUST 保持单核、同步和有界，不暗示完整 POSIX process/session 模型。

#### Scenario: 初始用户态进程具有确定性归属

- **WHEN** normal boot 启动 resident init 并进入默认 shell
- **THEN** init 与 shell MUST 拥有可查询的 `pid`、`pgid` 与 `sid`
- **AND** 这些值 MUST 在同一次运行中保持一致，直到进程退出或执行有界变更操作

#### Scenario: fork 继承归属

- **WHEN** 一个用户进程通过支持的 `fork` 路径创建子进程
- **THEN** 子进程 MUST 继承父进程当前的 `pgid` 与 `sid`
- **AND** 该继承 MUST 不改变父进程归属或其他无关进程归属

#### Scenario: exec 保持归属

- **WHEN** 一个用户进程通过支持的 `execve` 路径替换镜像
- **THEN** 新镜像 MUST 保持调用进程已有的 `pid`、`pgid` 与 `sid`
- **AND** 镜像替换 MUST NOT 隐式创建新 process group、session 或 terminal binding

### Requirement: 有界 process group 与 session 控制接口

BigOS SHALL 提供有限 syscall/libc wrapper 用于查询当前进程归属、查询目标进程归属、设置有界 process group、创建有界 session，以及报告确定性 errno。接口 MUST 运行在普通用户进程 syscall 上下文，MUST NOT 需要 IRQ、scheduler-critical 或 preemption-disabled 路径执行控制逻辑。

#### Scenario: 查询当前归属

- **WHEN** 简单静态用户程序调用支持的归属查询 wrapper
- **THEN** BigOS MUST 返回调用进程当前 `pid`、`pgid` 或 `sid` 的确定性值
- **AND** 查询失败 MUST 通过稳定 errno 或负错误码报告

#### Scenario: 设置 process group 成功

- **WHEN** 进程请求将自身或允许范围内的子进程放入同一 session 内的有效 process group
- **THEN** BigOS MUST 更新目标进程的 `pgid`
- **AND** 后续查询 MUST 观察到更新后的 `pgid`

#### Scenario: 设置 process group 拒绝无效目标

- **WHEN** 进程请求设置不存在进程、跨 session 目标、已不允许变更的目标或非法 `pgid`
- **THEN** BigOS MUST 拒绝该请求并返回确定性 errno
- **AND** 目标进程归属与终端前台绑定 MUST 保持不变

#### Scenario: 创建 session 成功

- **WHEN** 允许的进程请求创建新的有界 session
- **THEN** BigOS MUST 使该进程成为新 session 的成员并获得确定性 `sid` 与 `pgid` 归属
- **AND** 该操作 MUST NOT 自动创建多终端、伪终端或后台作业状态

### Requirement: 默认终端维护 foreground process group

BigOS SHALL 让默认控制台终端维护一个有界 foreground process group 绑定。该绑定 MUST 可由允许的 shell/用户进程通过普通 syscall 查询和设置，MUST 只引用当前默认终端可接受的有效 `pgid`，并 MUST 在失败时保持旧绑定不变。

#### Scenario: 查询默认终端前台组

- **WHEN** shell 或简单静态用户程序查询默认终端 foreground process group
- **THEN** BigOS MUST 返回当前绑定的 `pgid` 或确定性错误
- **AND** 查询 MUST 不改变任何进程、fd 或 terminal state

#### Scenario: 设置默认终端前台组

- **WHEN** shell 将同一 session 中存在的 process group 设置为默认终端 foreground group
- **THEN** 默认终端 MUST 记录该 `pgid` 为当前 foreground group
- **AND** 后续 foreground 查询和 terminal input targeting MUST 使用更新后的绑定

#### Scenario: 无效前台组设置失败

- **WHEN** 请求设置不存在、跨 session、空归属或不允许的 `pgid` 为默认终端 foreground group
- **THEN** BigOS MUST 返回确定性 errno
- **AND** 默认终端 MUST 保留原 foreground group

### Requirement: foreground terminal input targets foreground group

BigOS SHALL 将默认终端的 interrupt-like input 与当前 foreground process group 对齐。该输入 MAY 转换为面向 foreground group 的有界信号投递或等价的确定性 terminal event，但 MUST NOT 要求完整 POSIX terminal process group、`termios`、后台读写控制或 job control。

#### Scenario: interrupt-like input 作用于前台组

- **WHEN** 默认终端接收 interrupt-like input 且存在有效 foreground process group
- **THEN** BigOS MUST 使该 foreground group 中的可投递用户进程观察到有界中断结果
- **AND** 该结果 MUST 可通过进程终止、信号 handler、shell 状态或确定性验证输出观察

#### Scenario: 无有效前台组时行为确定

- **WHEN** 默认终端接收 interrupt-like input 但 foreground group 无效或为空
- **THEN** BigOS MUST 忽略该输入或报告确定性 bounded terminal result
- **AND** MUST NOT panic、破坏 shell fd、访问无效进程对象或执行不可阻塞上下文中的阻塞操作

### Requirement: 生命周期清理保护 terminal foreground binding

BigOS SHALL 在进程退出、reap 和 process group 失效时保护默认终端 foreground binding。绑定指向的 group 完全消失后，后续查询、设置或 shell 恢复 MUST 得到确定性结果，MUST NOT 使用已释放进程对象。

#### Scenario: 前台组进程退出后可恢复

- **WHEN** shell 启动的 foreground group 中所有子进程退出并被 wait/reap
- **THEN** shell MUST 能将默认终端 foreground group 恢复为 shell 自身 group
- **AND** 已退出子进程的对象释放 MUST NOT 留下可被 terminal 路径解引用的悬垂引用

#### Scenario: 查询失效前台组不崩溃

- **WHEN** 默认终端 foreground group 已不再对应活动进程组且用户程序查询该状态
- **THEN** BigOS MUST 返回确定性失效结果或错误
- **AND** 该查询 MUST NOT panic 或污染进程 registry

### Requirement: 有界验证覆盖 process group session terminal control

BigOS SHALL 提供可复现验证覆盖 process group、session 与 foreground terminal binding 的代表行为。验证 MUST 可通过构建结果、用户程序输出、shell 输出、serial/log 输出或其他确定性低层信号判定。

#### Scenario: 验证归属继承和变更

- **WHEN** 启用对应验证路径并在配置好的 emulator 环境中运行
- **THEN** 验证 MUST 覆盖 `fork` 继承 `pgid/sid`、`execve` 保持归属、合法设置成功和非法设置失败
- **AND** 结果 MUST 可由确定性输出或 marker 判定

#### Scenario: 验证前台终端绑定恢复

- **WHEN** shell 启动 foreground command 或单级 pipe 并等待其完成
- **THEN** 验证 MUST 观察默认终端 foreground group 切换到子进程组并最终恢复到 shell 组
- **AND** shell MUST 在成功、子进程失败和 setup 失败后保持可继续交互

#### Scenario: 不可用环境被记录

- **WHEN** x86_64 cross toolchain、xmake、QEMU/Bochs、display/ROM 依赖或磁盘镜像配置不可用
- **THEN** 对应 runtime validation MAY 被跳过
- **AND** validation record MUST 写明缺失条件、已执行替代检查和剩余风险

