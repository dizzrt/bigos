## ADDED Requirements

### Requirement: 进程身份与启动时间戳字段

BigOS SHALL 在进程对象中维护最小身份四元组 uid/gid/euid/egid 与启动墙钟时间戳，并在各创建路径下按规则初始化或继承，使进程生命周期具备可继承、可判定的身份结构。

#### Scenario: init 进程身份与时间戳

- **WHEN** PID 1 的 init 进程被创建
- **THEN** 其 uid/gid/euid/egid MUST 为 0（root）
- **AND** 其启动时间戳 MUST 取创建时刻的当前墙钟 Unix 秒

#### Scenario: ELF 创建路径初始化身份

- **WHEN** 进程通过 ELF 创建路径（非 fork）产生
- **THEN** 其 uid/gid/euid/egid MUST 默认初始化为 0（root，因当前无 login/身份变更来源）
- **AND** 其启动时间戳 MUST 取创建时刻的当前墙钟 Unix 秒

#### Scenario: fork 路径继承身份

- **WHEN** 进程通过 `fork` 产生
- **THEN** 其 uid/gid/euid/egid MUST 逐字段继承自父进程
- **AND** 其启动时间戳 MUST 取 fork 时刻的当前墙钟 Unix 秒

#### Scenario: 身份字段不破坏既有生命周期

- **WHEN** 进程进入既有的父子链接、`wait`/`exit`、zombie/reaper teardown 流程
- **THEN** 新增身份与时间戳字段 MUST NOT 改变既有进程生命周期状态机与回收语义
