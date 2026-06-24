## ADDED Requirements

### Requirement: IRQ-safe completion handoff 保持最小工作
中断驱动 completion 入口 MUST 只执行 token 校验、terminal 发布或 rejection、固定容量诊断更新和 scheduler wakeup；该入口 MUST NOT 发送 EOI、阻塞、动态分配、访问用户内存、执行 cache/filesystem policy 或依赖 hosted runtime。

#### Scenario: 合法 IRQ completion
- **WHEN** ATA IRQ 或等价完成源提交一个合法 pending 请求 token
- **THEN** completion 入口 MUST 发布 terminal 状态并唤醒等待者，且不得在 completion 入口发送 PIC/LAPIC EOI 或执行文件系统回写

### Requirement: completion rejection 不改变已发布状态
中断驱动 completion 模型 MUST 在 token 失效、generation 不匹配、请求已 terminal、设备身份不匹配或重复完成时拒绝 completion；拒绝路径 MUST NOT 改写已发布 terminal 状态。

#### Scenario: 迟到 IRQ 被拒绝
- **WHEN** 请求已经因 timeout 到达 terminal 状态，随后 IRQ 提交旧 token completion
- **THEN** completion 模型 MUST 保留 timeout 结果，记录迟到 IRQ 或 token stale 诊断，并不得再次唤醒等待者

### Requirement: wakeup 与 terminal 发布顺序确定
completion 入口 MUST 先使 terminal 状态对等待者可见，再执行 scheduler wakeup 或等价唤醒；等待者恢复后 MUST 能观察到完整的 terminal reason 和诊断状态。

#### Scenario: 等待线程恢复读取最终状态
- **WHEN** completion 入口唤醒正在等待块请求的线程
- **THEN** 被唤醒线程 MUST 能读取该请求的 terminal reason，而不得看到仍为 pending 或未初始化的 completion 状态

### Requirement: completion 诊断不扩大中断所有权
completion 诊断 MUST 保持 EOI-owner-neutral，并不得改变 exception、syscall、timer IRQ、keyboard IRQ 或未来 APIC 路径的中断所有权语义。

#### Scenario: completion 入口不处理 EOI
- **WHEN** 块设备 IRQ handler 调用 request-layer completion entry
- **THEN** completion entry MUST NOT 向 i8259 或 APIC 发送 EOI，EOI 仍由外层 IRQ dispatch 或设备 IRQ owner 按既有边界处理
