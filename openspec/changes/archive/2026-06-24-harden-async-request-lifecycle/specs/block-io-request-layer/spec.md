## ADDED Requirements

### Requirement: 请求层发布统一 terminal 原因
块 I/O 请求层 MUST 为同步 wrapper 和内部消费者发布统一 terminal reason，至少能区分 success、invalid request、queue full、issue failure、device error、timeout、cancel 或等价 completion rejection；调用者不得需要解析设备私有状态才能判断最终结果。

#### Scenario: 同步 wrapper 观察设备错误
- **WHEN** 设备完成源以 device error 结束一个已 pending 请求
- **THEN** 请求层 MUST 将该请求转为 terminal device error，并让同步提交调用返回确定性失败而不是 success 或无限等待

#### Scenario: queue full 可诊断
- **WHEN** 请求层无法为新请求分配队列槽位
- **THEN** 请求层 MUST 返回 queue full 或等价有界失败状态，且不得构造未 armed 的悬空 token

### Requirement: 槽位释放只发生在 terminal 观察后
请求队列槽位 MUST 只在请求达到 terminal 状态且等待者或同步 wrapper 已完成最终状态观察后进入可复用状态；槽位复用 MUST 更新 generation 或等价身份，防止旧 completion 影响新请求。

#### Scenario: 槽位复用生成新身份
- **WHEN** 一个 terminal 请求释放槽位后，新请求复用同一槽位
- **THEN** 请求层 MUST 为新请求生成新的可验证身份，使旧 token 的 completion 被拒绝

### Requirement: 请求层上下文边界覆盖生命周期错误
请求层 MUST 明确区分可阻塞提交/等待路径与 IRQ-safe completion 路径；生命周期错误处理 MUST 保持在允许的上下文内，IRQ-safe 路径不得执行分配、释放、阻塞等待、cache policy、filesystem policy 或用户内存访问。

#### Scenario: IRQ 完成唤醒等待者
- **WHEN** IRQ-safe completion entry 合法完成一个 pending 请求
- **THEN** 请求层 MUST 只发布 terminal 状态、更新有界诊断并唤醒等待者，不得在该路径执行同步 wrapper 的后续清理、cache writeback 或文件系统提交

### Requirement: 请求层诊断可区分生命周期失败
请求层 MUST 提供默认关闭验证可观察的有界诊断，用于区分 issue failure、timeout、cancel、device error、late completion、duplicate completion、identity mismatch 和 slot reuse protection。

#### Scenario: 验证读取诊断快照
- **WHEN** 默认关闭块 I/O 验证触发 timeout 后迟到 completion
- **THEN** 请求层诊断 MUST 能显示 timeout terminal 已发布且迟到 completion 被拒绝，验证不得只依赖最终返回码
