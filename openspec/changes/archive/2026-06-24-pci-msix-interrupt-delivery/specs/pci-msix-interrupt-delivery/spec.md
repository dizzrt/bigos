## ADDED Requirements

### Requirement: MSI-X capability 解析
BigOS SHALL 通过 PCI 配置访问解析设备的 MSI-X capability，读取 table size、MSI-X table 所在 BAR 索引与偏移、PBA 所在 BAR 索引与偏移，以及 function-level enable 与 function mask 控制位。解析 MUST 只在普通可阻塞内核上下文执行，MUST NOT 在 IRQ 上下文进行。

#### Scenario: 解析支持 MSI-X 的设备
- **WHEN** 一个设备的 capability list 中存在 MSI-X capability
- **THEN** BigOS MUST 返回 table size、table BAR 索引与偏移、PBA BAR 索引与偏移
- **AND** 它 MUST 能读取并区分 function-level enable 与 function mask 当前状态

#### Scenario: 设备不支持 MSI-X
- **WHEN** 一个设备的 capability list 中不存在 MSI-X capability
- **THEN** BigOS MUST 返回确定性的“不支持 MSI-X”状态
- **AND** 它 MUST NOT 伪造 table 信息或继续编程 MSI-X

#### Scenario: 不可阻塞上下文拒绝解析
- **WHEN** IRQ handler、调度临界区或抢占禁用区尝试解析 MSI-X capability
- **THEN** BigOS MUST 确定性拒绝或进入有记录的诊断路径
- **AND** 它 MUST NOT 在该上下文执行 capability/BAR 访问

### Requirement: MSI-X table 与 PBA 映射
BigOS SHALL 通过现有内核虚拟内存能力为 MSI-X table 与 PBA 所在内存 BAR 区域建立有界 MMIO 映射（不可缓存），用于读写 table 条目与读取 pending 位。映射失败 MUST 返回确定性错误。

#### Scenario: 建立 table/PBA 映射
- **WHEN** MSI-X capability 指向某个内存 BAR 内的 table/PBA 偏移
- **THEN** BigOS MUST 为该区域建立有界 MMIO 映射并可访问 table 条目与 PBA
- **AND** 它 MUST NOT 依赖该 BAR 落在 direct map 范围的假设

#### Scenario: 映射失败确定性处理
- **WHEN** MSI-X table/PBA 的 MMIO 映射无法建立
- **THEN** BigOS MUST 返回确定性映射失败状态
- **AND** 它 MUST NOT 在未建立映射的情况下访问 table/PBA 地址

### Requirement: MSI-X 条目编程
BigOS SHALL 为使用的 MSI-X table 条目编程 message address 与 message data。message address MUST 指向 LAPIC 并编码目标 APIC ID；message data MUST 编码由内核向量分配能力分配的中断向量。编程顺序 MUST 遵循 mask-先行：在 message 未编程完成前对应条目 MUST 保持屏蔽。

#### Scenario: 编程一个条目指向已分配向量
- **WHEN** 内核为一个 MSI-X 条目分配了内核中断向量并请求编程该条目
- **THEN** BigOS MUST 在条目处于屏蔽状态下写入 message address（LAPIC + 目标 APIC ID）与 message data（该向量）
- **AND** 编程完成后 MUST 能按需清除该条目的 per-vector mask

#### Scenario: 编程前保持屏蔽
- **WHEN** 一个 MSI-X 条目尚未完成 message 编程
- **THEN** 该条目 MUST 保持 per-vector mask 或 function mask 屏蔽
- **AND** BigOS MUST NOT 在编程完成前对该条目 unmask

#### Scenario: 向量与触发一致
- **WHEN** 一个已编程并 unmask 的 MSI-X 条目被设备或可控 producer 触发
- **THEN** 实际调用的 handler MUST 是该条目 message data 所编码向量注册的 handler
- **AND** 该向量完成后 MUST 通过 LAPIC EOI 边界发送且仅发送一次 EOI

### Requirement: MSI-X enable 与 mask 控制
BigOS SHALL 提供 function-level MSI-X enable/disable 与 per-vector mask/unmask 控制。启用顺序 MUST 保证在所有使用条目编程完成前不投递中断；mask 时设备 pending 的中断 MUST 在 unmask 后按 MSI-X 语义投递或由 PBA 反映。

#### Scenario: 启用顺序安全
- **WHEN** 内核完成所有使用条目的编程后启用 MSI-X function
- **THEN** BigOS MUST 在编程完成后才 function-level enable，并对需要的条目 unmask
- **AND** 在 enable/unmask 之前 MUST NOT 让任何使用条目投递中断

#### Scenario: per-vector mask 抑制投递
- **WHEN** 一个已编程条目被 per-vector mask 屏蔽且设备产生该中断
- **THEN** BigOS MUST NOT 在屏蔽期间调用该向量 handler
- **AND** 解除屏蔽后该中断 MUST 能按 MSI-X 语义被投递或通过 PBA 观察到 pending

#### Scenario: 禁用 MSI-X 不破坏其他中断
- **WHEN** 内核 function-level 禁用某设备的 MSI-X
- **THEN** BigOS MUST 停止该设备 MSI-X 向量的投递
- **AND** 它 MUST NOT 改变异常、syscall、timer、keyboard 或 IPI 等其他中断的所有权与语义

### Requirement: MSI-X 复用现有 EOI 与分发边界
BigOS SHALL 让 MSI-X 向量复用现有 `InterruptFrame` 分发 ABI 与 LAPIC EOI 所有权，不引入独立 EOI 路径。MSI-X 向量 MUST 标注为 LAPIC 拥有 EOI 的外部中断向量。

#### Scenario: MSI-X 向量走 LAPIC EOI
- **WHEN** 一个 MSI-X 向量完成其 handler
- **THEN** BigOS MUST 通过 LAPIC EOI 边界发送且仅发送一次 EOI
- **AND** 它 MUST NOT 为 MSI-X 向量发送 i8259 PIC EOI

#### Scenario: MSI-X 不改变异常与 syscall 语义
- **WHEN** MSI-X 投递处于活动状态
- **THEN** CPU 异常向量与 syscall 向量 0x80 的 EOI 与分发语义 MUST 保持不变
- **AND** MSI-X 配置 MUST NOT 改写这些路径的 handler 或所有权

### Requirement: MSI-X 投递验证可复现
BigOS SHALL 为 MSI-X 中断投递提供确定性验证，通过源级检查与默认关闭的运行时 smoke 覆盖（在仿真器与工具链可用时）。验证 MUST 覆盖 capability 解析、条目编程、mask/unmask 抑制与恢复、以及向量投递 handler 闭环。

#### Scenario: smoke 覆盖 MSI-X 投递闭环
- **WHEN** 在具备预期工具链与支持 MSI-X 的 QEMU 设备或可控 producer 的环境中启用该验证路径
- **THEN** 验证 MUST 解析一个 MSI-X capability、编程至少一个条目、触发该向量并观察其 handler 执行
- **AND** 它 MUST 覆盖 mask 抑制投递与 unmask 恢复投递，并按既有默认关闭 smoke 风格输出确定性 pass/fail 诊断

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU、Bochs、交叉 binutils、ROM/display 依赖、MSI-X 支持或所需仿真器配置不可用
- **THEN** 验证记录 MUST 记录被跳过的覆盖项与残余风险
- **AND** 它 MUST NOT 对被跳过的环境声明运行时 smoke 成功

### Requirement: MSI-X 不扩大启动与能力边界
BigOS SHALL 在引入 MSI-X 投递时不改变当前 x86_64 Legacy BIOS 启动 ABI、链接地址、页表布局、既有中断/系统调用向量分配、磁盘布局或用户可见 userland 行为，并保持能力描述有界。

#### Scenario: 默认启动路径保持兼容
- **WHEN** MSI-X 能力存在于正常 x86_64 Legacy BIOS 启动中且默认关闭验证未启用
- **THEN** 内核 MUST 保持既有 boot handoff、固定向量分配、页表布局与用户可见进程行为不变
- **AND** MSI-X MUST NOT 要求 IOMMU、中断重映射、SMP 中断重排或新存储后端

#### Scenario: 能力边界不被夸大
- **WHEN** 文档或验证描述 MSI-X 能力
- **THEN** 它 MUST 将其标识为有界的内核内部 MSI-X 投递边界
- **AND** 它 MUST NOT 声称 MSI（非 X）、IRQ affinity 动态迁移、中断重映射、中断合并或用户可见中断 ABI
