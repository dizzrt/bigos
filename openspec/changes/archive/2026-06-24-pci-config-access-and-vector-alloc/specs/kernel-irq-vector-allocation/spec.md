## ADDED Requirements

### Requirement: 有界内核中断向量分配
BigOS SHALL 在现有静态 IDT、ISR 注册和 LAPIC EOI 边界之上提供有界的内核中断向量分配能力。分配 MUST 从一个不与异常向量、PIC remap 向量、syscall 向量及现有 LAPIC 固定向量冲突的受限保留区间取值，分配出的向量 MUST 标注为 LAPIC 拥有 EOI 的外部中断向量。分配与释放 MUST 只在普通可阻塞内核上下文执行。

#### Scenario: 分配并注册一个向量
- **WHEN** 驱动初始化在普通可阻塞上下文请求一个中断向量并提供 handler
- **THEN** BigOS MUST 从受限保留区间返回一个未占用向量，并通过现有 ISR 注册绑定该 handler
- **AND** 该向量的 EOI 所有权 MUST 标注为 LAPIC，与现有外部中断 EOI 边界一致

#### Scenario: 向量区间耗尽
- **WHEN** 受限保留向量区间已无可用槽位时再次请求分配
- **THEN** BigOS MUST 返回确定性的“无可用向量”错误
- **AND** 它 MUST NOT 复用仍在使用的向量或越界占用固定向量

#### Scenario: 释放后可重新分配且拒绝重复释放
- **WHEN** 一个已分配向量被释放后再次发起分配请求
- **THEN** BigOS MAY 重新分配该向量给新的请求
- **AND** 对同一向量的重复释放 MUST 被确定性拒绝或诊断，且 MUST NOT 破坏其他向量的占用状态

#### Scenario: 不破坏现有固定向量
- **WHEN** 向量分配能力处于活动状态
- **THEN** 异常向量、PIC remap 向量、syscall 向量 0x80 与现有 LAPIC timer/IPI 向量的所有权与语义 MUST 保持不变
- **AND** 动态分配 MUST NOT 改写这些固定向量的 handler 或 EOI 所有权

### Requirement: 分配向量复用现有 EOI 与分发边界
BigOS SHALL 让分配出的向量在触发时复用现有 `InterruptFrame` 分发 ABI 与 LAPIC EOI 所有权，不引入独立 EOI 路径。分配向量的 handler 调用约定 MUST 与既有外部中断 handler 一致。

#### Scenario: 分配向量触发走现有分发
- **WHEN** 一个已分配并注册的向量被投递
- **THEN** BigOS MUST 通过现有 `InterruptFrame` 分发路径调用其注册的 handler
- **AND** 该向量完成后 MUST 通过 LAPIC EOI 边界发送且仅发送一次 EOI

#### Scenario: 分配向量不触碰 PIC EOI
- **WHEN** 一个分配向量完成其 handler
- **THEN** BigOS MUST NOT 为该向量发送 i8259 PIC EOI
- **AND** 它 MUST NOT 改变异常或 syscall 路径的 EOI 语义

### Requirement: 向量分配验证可复现
BigOS SHALL 为内核中断向量分配提供确定性验证，通过源级检查与默认关闭的运行时 smoke 覆盖（在仿真器与工具链可用时）。验证 MUST 覆盖向量分配、释放、重复释放与区间耗尽边界。

#### Scenario: smoke 覆盖向量生命周期
- **WHEN** 在具备预期工具链与仿真器的环境中启用该验证路径
- **THEN** 验证 MUST 完成至少一次向量分配与释放，并覆盖重复释放与耗尽的确定性错误
- **AND** 它 MUST 按既有默认关闭 smoke 风格输出确定性 pass/fail 诊断

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU、Bochs、交叉 binutils、ROM/display 依赖或所需仿真器配置不可用
- **THEN** 验证记录 MUST 记录被跳过的覆盖项与残余风险
- **AND** 它 MUST NOT 对被跳过的环境声明运行时 smoke 成功

### Requirement: 向量分配不扩大启动与硬件边界
BigOS SHALL 在引入向量分配时不改变当前 x86_64 Legacy BIOS 启动 ABI、链接地址、页表布局、既有中断/系统调用向量分配、磁盘布局或用户可见 userland 行为，并保持能力描述有界。

#### Scenario: 默认启动路径保持兼容
- **WHEN** 向量分配能力存在于正常 x86_64 Legacy BIOS 启动中且默认关闭验证未启用
- **THEN** 内核 MUST 保持既有 boot handoff、固定向量分配、页表布局与用户可见进程行为不变
- **AND** 向量分配 MUST NOT 要求 MSI/MSI-X、SMP 向量迁移或新存储后端

#### Scenario: 能力边界不被夸大
- **WHEN** 文档或验证描述向量分配能力
- **THEN** 它 MUST 将其标识为有界的内核内部向量分配边界
- **AND** 它 MUST NOT 声称完整 IRQ affinity、MSI/MSI-X 编程或动态中断子系统重构
