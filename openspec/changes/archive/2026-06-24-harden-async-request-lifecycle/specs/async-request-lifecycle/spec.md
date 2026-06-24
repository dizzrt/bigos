## ADDED Requirements

### Requirement: 请求生命周期状态有界且单调
内核块 I/O 请求 MUST 通过有界状态机表达从槽位分配、armed、issued、pending、completion、terminal 到可复用的生命周期；除显式回收路径外，状态转换 MUST 单调前进，且不得依赖动态分配、hosted runtime、异常或 RTTI。

#### Scenario: 请求成功完成后回收
- **WHEN** 一个有效块请求被分配槽位、成功 issue，并由合法 completion token 发布成功状态
- **THEN** 请求层 MUST 将该请求 exactly-once 转为 terminal success，唤醒等待者，并只在等待者观察最终状态后释放槽位以供后续复用

#### Scenario: issue 失败不遗留 pending 请求
- **WHEN** 设备 issue 在请求进入硬件 pending 前返回失败
- **THEN** 请求层 MUST 将请求转为 terminal issue-failed 或等价失败状态，释放或标记槽位为可回收，且不得让同步 wrapper 无限等待 completion

### Requirement: terminal 状态 exactly-once 发布
每个请求 MUST 最多发布一次 terminal 状态；timeout、cancel、device error、issue failure、success 和 completion rejection 之间的竞争 MUST 由请求层裁决，后续完成不得覆盖已观察到的 terminal 结果。

#### Scenario: timeout 赢得竞争
- **WHEN** 请求等待超时后被请求层转为 terminal timeout，随后设备 IRQ 或 producer 提交同一请求的迟到 success completion
- **THEN** 请求层 MUST 拒绝该迟到 completion，保留 timeout 作为同步 wrapper 和 cache/writeback 可观察的最终状态

#### Scenario: 重复完成被拒绝
- **WHEN** 一个已 terminal success 的请求再次收到相同 token 的 completion
- **THEN** 请求层 MUST 拒绝第二次 completion，更新有界诊断，并不得再次唤醒、释放或改写该请求状态

### Requirement: token 身份防止槽位复用污染
completion token MUST 绑定至少设备身份、队列槽位、generation 和请求身份中的可验证组合；请求层 MUST 拒绝跨设备、跨槽位、generation 不匹配或已失效 token 的 completion。

#### Scenario: generation 不匹配
- **WHEN** 旧请求 timeout 后槽位被复用并分配新 generation，而旧设备完成源提交旧 generation token
- **THEN** 请求层 MUST 拒绝该 completion，保持新请求状态不变，并记录 generation mismatch 或等价诊断

#### Scenario: 设备身份不匹配
- **WHEN** completion token 的设备身份与目标队列槽位所属设备不一致
- **THEN** 请求层 MUST 拒绝该 completion，且不得唤醒错误设备队列上的等待线程

### Requirement: 取消和超时保持 fail-closed
请求 timeout、取消或等价失效路径 MUST 在请求层形成确定性 terminal 状态；如果底层硬件不能真实取消请求，请求层 MUST 拒绝后续迟到 completion，并让设备错误恢复留在驱动的有界路径中。

#### Scenario: 硬件不支持取消
- **WHEN** ATA PIO 或等价设备无法在 timeout 时安全撤销已发出的硬件命令
- **THEN** 请求层 MUST 返回 terminal timeout 或 cancel 状态，后续硬件完成 MUST 只能进入迟到 completion 诊断或驱动恢复路径

### Requirement: 生命周期诊断固定容量
异步请求生命周期诊断 MUST 使用固定容量状态、计数器、短码或快照；IRQ/completion 路径 MUST NOT 执行无界日志拼接、动态分配、阻塞输出或访问用户内存。

#### Scenario: completion 被拒绝时记录诊断
- **WHEN** 请求层拒绝迟到、重复或身份不匹配的 completion
- **THEN** 系统 MUST 更新固定容量诊断状态，使默认关闭验证能区分拒绝原因，而不在 IRQ 路径执行无界格式化
