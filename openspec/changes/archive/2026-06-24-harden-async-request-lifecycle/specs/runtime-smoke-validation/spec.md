## ADDED Requirements

### Requirement: Runtime smoke validation covers async request lifecycle
运行时 smoke 验证矩阵 MUST 包含默认关闭的异步请求生命周期验证，覆盖 success、issue failure、device error、timeout、cancel 或等价失效路径、重复 completion、迟到 completion、identity mismatch 和 slot reuse protection。

#### Scenario: 生命周期 smoke 触发迟到 completion
- **WHEN** 默认关闭验证主动让请求 timeout 后再提交旧 token completion
- **THEN** 验证 MUST 观测到 timeout terminal 结果、迟到 completion rejection 和未被污染的后续槽位复用

#### Scenario: 生命周期 smoke 覆盖重复完成
- **WHEN** 默认关闭验证对已 terminal 请求提交第二次 completion
- **THEN** 验证 MUST 观测到 duplicate completion rejection，且原 terminal 状态保持不变

### Requirement: Runtime smoke validation records diagnostic boundaries
运行时验证记录 MUST 区分通过检查、跳过检查、环境缺失、历史低层风险、当前变更新增诊断和剩余运行风险；当 QEMU、Bochs、ROM/display、磁盘镜像或 cross-toolchain 不可用时，记录 MUST 明确说明。

#### Scenario: emulator 不可用
- **WHEN** 本地无法运行 QEMU 或 Bochs lifecycle smoke
- **THEN** 验证记录 MUST 明确列出不可用组件、已执行的替代检查和剩余风险，不得声称完成运行时验证

### Requirement: Runtime smoke validation preserves default boot regression coverage
异步请求生命周期验证 MUST 保留默认启动回归覆盖；默认关闭 smoke 不得改变 normal PID-1/init、`/bin/sh`、默认 boot disk 或默认块设备选择。

#### Scenario: 默认关闭验证未启用
- **WHEN** 未启用异步请求生命周期 smoke
- **THEN** 默认启动路径 MUST 保持既有 normal userland baseline 行为，验证专用 producer、诊断触发器或故障注入不得参与默认启动

### Requirement: Runtime smoke validation covers cache and writeback failure retention
运行时验证 MUST 覆盖 cache round-trip、dirty writeback failure retention、dirty victim eviction failure 和 persistent `/rw` clean-sync failure，以确认生命周期 terminal failure 不会被误映射为 durable success。

#### Scenario: dirty writeback 失败
- **WHEN** 默认关闭验证注入底层 request timeout 或 device error
- **THEN** 验证 MUST 观测到 dirty 或 pending-writeback 状态被保留，且 `sync`/`fsync` 或等价调用返回确定性失败
