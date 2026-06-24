## ADDED Requirements

### Requirement: 有界 PCI 配置空间读访问
BigOS SHALL 提供 freestanding-safe 的 PCI 配置空间读访问能力，基于传统 0xCF8/0xCFC 端口机制，按 bus、device、function、offset 进行 32 位对齐 dword 读，并在其上派生 8/16 位读。配置访问 MUST 只在普通可阻塞内核上下文执行，MUST NOT 在 IRQ 上下文、调度临界区或抢占禁用区进行。

#### Scenario: 读取存在设备的配置头
- **WHEN** 内核初始化在普通可阻塞上下文请求一个存在的 bus/device/function 的 vendor/device ID
- **THEN** BigOS MUST 通过 0xCF8/0xCFC 机制返回该设备的真实 vendor/device ID
- **AND** 8/16 位派生读 MUST 与对应 dword 读的掩码移位结果一致

#### Scenario: 探测不存在的设备
- **WHEN** 内核读取一个不存在的 bus/device/function 的 vendor ID
- **THEN** BigOS MUST 返回确定性的“无设备”指示（如全 1 vendor ID 判定）
- **AND** 它 MUST NOT 把无效响应当作有效设备继续解析

#### Scenario: 不可阻塞上下文拒绝配置访问
- **WHEN** IRQ handler、timer tick、调度临界区或抢占禁用区尝试发起 PCI 配置访问
- **THEN** BigOS MUST 确定性拒绝或进入有记录的诊断路径
- **AND** 它 MUST NOT 在该上下文执行端口 IO 配置访问

### Requirement: capability list 遍历
BigOS SHALL 支持在设备 status 寄存器声明 capability list 时，从 capabilities pointer 跟随并遍历 capability 链表，返回每个 capability 的 ID 与配置空间偏移。遍历 MUST 有界，能检测循环或越界指针并安全终止。

#### Scenario: 遍历声明了 capability 的设备
- **WHEN** 一个设备 status 寄存器声明存在 capability list 且 capabilities pointer 有效
- **THEN** BigOS MUST 依次返回链表中每个 capability 的 ID 与偏移
- **AND** 遍历 MUST 在到达链表末尾时确定性终止

#### Scenario: 防御非法 capability 指针
- **WHEN** capability 链表包含越界偏移、未对齐指针或自指/循环链接
- **THEN** BigOS MUST 在有界步数内停止遍历
- **AND** 它 MUST NOT 进入无限循环或读取配置空间界外

#### Scenario: 设备未声明 capability list
- **WHEN** 设备 status 寄存器未声明 capability list
- **THEN** BigOS MUST 返回空遍历结果
- **AND** 它 MUST NOT 解引用未定义的 capabilities pointer

### Requirement: BAR 描述读取
BigOS SHALL 读取 PCI 设备 BAR 的原始值并解析出地址类型（IO 空间或内存空间）、内存 BAR 的位宽（32 位或 64 位）、可预取标志和区域大小。size 探测 MUST 只作用于 BAR 偏移，并在写探测值后恢复 BAR 原值。本能力 MUST NOT 强制映射设备内存。

#### Scenario: 解析内存 BAR
- **WHEN** 内核读取一个内存类型 BAR
- **THEN** BigOS MUST 返回其基址、内存空间类型、32/64 位宽度与探测出的区域大小
- **AND** 它 MUST NOT 要求调用方先建立 MMIO 映射即可获得该描述

#### Scenario: 解析 IO BAR
- **WHEN** 内核读取一个 IO 类型 BAR
- **THEN** BigOS MUST 标识其为 IO 空间并返回 IO 端口基址与大小
- **AND** 它 MUST NOT 把 IO BAR 误判为内存 BAR

#### Scenario: size 探测后恢复 BAR
- **WHEN** BigOS 通过写全 1 再回读探测 BAR 大小
- **THEN** 它 MUST 在探测后把 BAR 寄存器恢复为原始值
- **AND** 它 MUST NOT 把探测写作用于非 BAR 配置寄存器

### Requirement: PCI 配置访问验证可复现
BigOS SHALL 为 PCI 配置访问提供确定性验证，通过源级检查与默认关闭的运行时 smoke 覆盖（在仿真器与工具链可用时）。验证 MUST 覆盖设备探测、capability 遍历与 BAR 描述读取。

#### Scenario: smoke 覆盖探测与遍历
- **WHEN** 在具备预期工具链与 QEMU PCI 设备的环境中启用该验证路径
- **THEN** 验证 MUST 探测至少一个已知 PCI 设备、遍历其 capability、并读取至少一个 BAR 描述
- **AND** 它 MUST 按既有默认关闭 smoke 风格输出确定性 pass/fail 诊断

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU、Bochs、交叉 binutils、ROM/display 依赖或所需仿真器配置不可用
- **THEN** 验证记录 MUST 记录被跳过的覆盖项与残余风险
- **AND** 它 MUST NOT 对被跳过的环境声明运行时 smoke 成功

### Requirement: PCI 配置访问不扩大启动与硬件边界
BigOS SHALL 在引入 PCI 配置访问时不改变当前 x86_64 Legacy BIOS 启动 ABI、链接地址、页表布局、既有中断/系统调用向量分配、磁盘布局或用户可见 userland 行为，并保持能力描述有界。

#### Scenario: 默认启动路径保持兼容
- **WHEN** PCI 配置访问存在于正常 x86_64 Legacy BIOS 启动中且默认关闭验证未启用
- **THEN** 内核 MUST 保持既有 boot handoff、向量分配、页表布局与用户可见进程行为不变
- **AND** PCI 配置访问 MUST NOT 要求 ECAM、MSI/MSI-X 或新存储后端

#### Scenario: 能力边界不被夸大
- **WHEN** 文档或验证描述 PCI 配置访问能力
- **THEN** 它 MUST 将其标识为有界的内核内部 PCI 配置访问边界
- **AND** 它 MUST NOT 声称完整 PCI 枚举、ECAM、热插拔、PCIe 扩展能力或用户可见设备模型
