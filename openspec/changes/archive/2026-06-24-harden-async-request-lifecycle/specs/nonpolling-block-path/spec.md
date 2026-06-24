## ADDED Requirements

### Requirement: 非轮询块路径暴露生命周期失败边界
非轮询块路径 MUST 通过请求层生命周期 terminal reason 暴露 issue failure、device error、timeout、cancel、completion rejection 和 queue failure；设备后端不得用私有同步轮询返回值绕过请求层状态机。

#### Scenario: ATA 路径 timeout
- **WHEN** ATA-backed 非轮询请求未在有界等待内完成
- **THEN** 请求层 MUST 返回 terminal timeout，后续迟到 ATA completion MUST 被拒绝或进入驱动恢复诊断，而不得覆盖 timeout 结果

#### Scenario: 同步兼容 producer 仍走生命周期
- **WHEN** RAM 或验证 producer 可以立即完成请求
- **THEN** 它 MUST 通过同一 request-layer lifecycle 发布 terminal 状态，而不得直接绕过 completion/token 身份检查

### Requirement: cache/writeback 成功边界不随诊断变化
非轮询块路径的生命周期诊断 MUST NOT 改变 page/buffer cache、dirty writeback、dirty victim eviction、`sync`/`fsync` 或 persistent `/rw` clean-sync 的外部成功语义；只有 request-layer terminal success 才能清除对应 dirty 或 pending-writeback 状态。

#### Scenario: writeback failure 保留 dirty
- **WHEN** dirty writeback 底层请求以 timeout、device error、issue failure 或 completion rejection 结束
- **THEN** cache/writeback 层 MUST 保留 dirty 或 pending-writeback 状态，并向调用者传播确定性失败

### Requirement: 非轮询诊断保持 freestanding-safe
非轮询块路径诊断 MUST 使用有界状态、短码或固定容量快照，并在 IRQ/completion 路径避免动态分配、阻塞输出、用户内存访问、长字符串格式化和大型栈对象。

#### Scenario: 设备错误诊断
- **WHEN** 设备后端发布 terminal device error
- **THEN** 非轮询路径 MUST 记录有界错误分类，使验证能区分 device error 与 timeout 或 issue failure，而不在 IRQ 路径生成无界日志

### Requirement: 默认启动不依赖验证专用 producer
非轮询块路径的正常启动和文件系统 I/O MUST 使用真实配置下的块设备完成路径；验证专用 producer MUST 保持默认关闭，并不得成为默认启动到达用户态的必要条件。

#### Scenario: 默认启动路径
- **WHEN** 未启用默认关闭块 I/O smoke
- **THEN** normal init 和用户态 baseline MUST 仍通过真实块设备路径运行，不得依赖验证 producer 才能完成 boot-time 读写
