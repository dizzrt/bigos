## ADDED Requirements

### Requirement: modern-only virtio-blk 设备发现与发布
BigOS SHALL 通过设备/驱动框架发现、probe 并发布一个 modern-only virtio-blk 块设备后端，使用内核内部稳定角色。probe MUST 只在普通可阻塞内核上下文执行，发现失败 MUST 确定性记录且不发布不可用设备。

#### Scenario: 发现并发布 virtio-blk
- **WHEN** PCI 上存在一个 modern virtio-blk 设备且 probe 在可阻塞上下文执行成功
- **THEN** BigOS MUST 通过设备框架发布该块设备后端，并可经内核内部稳定角色查找其块接口
- **AND** 该角色 MUST NOT 暴露为用户可见 syscall ABI、文件系统设备编号或外部验证 ABI

#### Scenario: 设备缺失或不支持时不发布
- **WHEN** 没有 modern virtio-blk 设备，或设备不满足 modern-only/MSI-X 前提
- **THEN** BigOS MUST 记录确定性的不可用状态并保持设备未发布
- **AND** 它 MUST NOT 把失败设备作为可用块设备返回

#### Scenario: 不可阻塞上下文不执行 probe
- **WHEN** IRQ 上下文、调度临界区或抢占禁用区尝试 probe virtio-blk
- **THEN** BigOS MUST 确定性拒绝或进入有记录的诊断路径
- **AND** 它 MUST NOT 在该上下文执行 PCI/virtqueue 初始化

### Requirement: virtio modern PCI transport 初始化
BigOS SHALL 通过 PCI capability 解析 virtio common/notify/ISR/device 配置结构，按 virtio 1.0+ 步骤执行 device reset、ACKNOWLEDGE/DRIVER status 推进、feature 协商（含 VIRTIO_F_VERSION_1）、FEATURES_OK 校验与 DRIVER_OK。任一步骤失败 MUST 置 FAILED status 并诊断。

#### Scenario: 成功完成 transport 初始化
- **WHEN** 一个 modern virtio-blk 设备被 probe
- **THEN** BigOS MUST 解析 virtio PCI capability 并映射所需 cfg 区域，按顺序推进 status 并完成 feature 协商
- **AND** 仅当 FEATURES_OK 被设备接受后 MUST 继续初始化 virtqueue 并最终置 DRIVER_OK

#### Scenario: feature 协商失败
- **WHEN** 设备不接受 VIRTIO_F_VERSION_1 或必需 feature 集合
- **THEN** BigOS MUST 置设备 FAILED status 并记录确定性诊断
- **AND** 它 MUST NOT 在未完成协商时提交块请求

#### Scenario: 仅支持 modern transport
- **WHEN** 设备仅提供 legacy 接口而无 modern virtio PCI capability
- **THEN** BigOS MUST 判定为不支持并不发布
- **AND** 它 MUST NOT 回退到 legacy IO 端口布局

### Requirement: split virtqueue 初始化与提交
BigOS SHALL 初始化单个 split virtqueue（descriptor table、available ring、used ring），使用对齐的内核物理页并以物理地址提供给设备，队列深度为有界静态容量。块请求提交 MUST 构造 virtio-blk 请求头、数据缓冲与状态字节的 descriptor 链，写入 available ring 并 notify 设备。

#### Scenario: 初始化 virtqueue
- **WHEN** transport 初始化进入 virtqueue 配置阶段
- **THEN** BigOS MUST 分配并对齐 descriptor/available/used 结构，并将其物理地址写入 virtio common cfg 的 queue 字段
- **AND** 队列容量 MUST 为有界静态值且不依赖运行时无界增长

#### Scenario: 提交块读请求
- **WHEN** 块请求层提交一个有效读请求给已发布 virtio-blk 后端
- **THEN** BigOS MUST 构造读方向的 descriptor 链（请求头 + 设备可写数据缓冲 + 状态字节），写入 available ring 并 notify 设备
- **AND** 请求 MUST 进入 pending 状态等待 used 完成

#### Scenario: 提交块写请求
- **WHEN** 块请求层提交一个有效写请求给已发布 virtio-blk 后端
- **THEN** BigOS MUST 构造写方向的 descriptor 链（请求头 + 设备可读数据缓冲 + 状态字节），写入 available ring 并 notify 设备
- **AND** 写完成状态 MUST 仅在设备通过 used ring 报告成功且状态字节为成功后报告成功

### Requirement: MSI-X 完成中断接入块请求层
BigOS SHALL 将 virtio-blk used buffer 完成中断通过 MSI-X 向量投递，并在 IRQ-safe completion 入口解析 used ring，按块请求层 token 身份完成已 pending 请求并唤醒等待者。completion 入口 MUST 保持 allocation-free、nonblocking、不发送 PIC EOI、不做 cache/filesystem policy。

#### Scenario: used 完成唤醒等待者
- **WHEN** 设备完成一个已提交请求并通过 MSI-X 投递 used 通知
- **THEN** completion 入口 MUST 解析 used ring，按 token 身份将对应 pending 请求置 terminal 状态并唤醒等待者
- **AND** 同步等待的提交者 MUST 观察到与 used 结果一致的最终请求层状态

#### Scenario: completion 入口保持 IRQ-safe
- **WHEN** virtio-blk MSI-X 完成入口运行
- **THEN** 它 MUST 只更新有界请求/完成状态并唤醒等待者，并通过 LAPIC EOI 边界发送 EOI
- **AND** 它 MUST NOT 分配/释放内存、阻塞、提交新请求、做 cache writeback、访问文件系统或发送 i8259 PIC EOI

#### Scenario: 迟到或不匹配完成被拒绝
- **WHEN** used 通知对应的请求已 timeout/cancel 或 token generation 不匹配
- **THEN** BigOS MUST 拒绝或诊断该完成，保留既有 terminal 结果
- **AND** 它 MUST NOT 再次唤醒该等待者或完成复用队列槽的新请求

### Requirement: virtio-blk 错误与 timeout 确定性
BigOS SHALL 将 virtio-blk 设备错误（状态字节非成功）、传输失败与请求 timeout 映射为块请求层确定性失败状态，区别于成功，并保持与 ATA 路径一致的生命周期语义。

#### Scenario: 设备报告错误状态
- **WHEN** 设备在 used 完成中返回非成功的 virtio-blk 状态字节
- **THEN** BigOS MUST 将该请求置为确定性 device error 终态并传播失败
- **AND** 它 MUST NOT 报告该请求成功

#### Scenario: 请求 timeout
- **WHEN** 一个已提交请求在有界等待内未收到 used 完成
- **THEN** BigOS MUST 返回确定性 timeout 状态，后续迟到完成 MUST 被拒绝或诊断
- **AND** timeout 结果 MUST NOT 被迟到完成覆盖

### Requirement: virtio-blk 不改变默认启动与磁盘布局
BigOS SHALL 在引入 virtio-blk 时不改变当前 x86_64 Legacy BIOS 启动 ABI、链接地址、页表布局、中断/系统调用向量分配、boot disk/exFAT/persistent `/rw` 布局或默认 ATA/userland baseline。virtio-blk 角色 MUST 保持内核内部，默认启动 MUST NOT 依赖 virtio-blk。

#### Scenario: 默认启动路径保持兼容
- **WHEN** virtio-blk 驱动编译进内核但默认关闭验证未启用
- **THEN** 默认启动 MUST 仍通过既有 ATA/exFAT 路径到达 userland baseline
- **AND** 它 MUST NOT 要求 virtio-blk 来挂载启动文件系统或启动 userland

#### Scenario: 角色保持内核内部
- **WHEN** virtio-blk 后端被注册并发布
- **THEN** 用户程序与文件系统路径 MUST NOT 因此获得新设备节点、syscall 可见设备 id 或持久挂载名
- **AND** 验证 MUST 通过内核内部框架查找选择该后端

### Requirement: virtio-blk 验证可复现
BigOS SHALL 为 virtio-blk 提供确定性验证，通过源级检查与默认关闭运行时 smoke 覆盖（在仿真器与工具链可用时）。验证 MUST 覆盖设备发布、读写往返、设备错误/timeout、MSI-X 完成闭环与默认启动回归。

#### Scenario: smoke 覆盖读写与完成闭环
- **WHEN** 在具备预期工具链与支持 modern virtio-blk + MSI-X 的 QEMU 环境中启用该验证路径
- **THEN** 验证 MUST 发布设备、提交至少一次写与一次读并校验数据往返一致，且通过 MSI-X 完成观察同步等待者恢复
- **AND** 它 MUST 覆盖至少一个确定性失败（设备错误或 timeout），并按既有默认关闭 smoke 风格输出 pass/fail

#### Scenario: 默认启动回归被覆盖
- **WHEN** 默认关闭验证未启用
- **THEN** 验证记录 MUST 确认默认启动 boot/shell/`/rw`/userland baseline 行为不变
- **AND** virtio-blk MUST NOT 成为默认启动到达用户态的必要条件

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU、Bochs、交叉 binutils、ROM/display 依赖、virtio-blk/MSI-X 支持或磁盘镜像配置不可用
- **THEN** 验证记录 MUST 记录被跳过的覆盖项与残余风险
- **AND** 它 MUST NOT 对被跳过的环境声明运行时 smoke 成功

### Requirement: virtio-blk 能力边界不被夸大
BigOS SHALL 保持 virtio-blk 能力描述有界，不声称未实现的存储或中断能力。

#### Scenario: 文档限定能力范围
- **WHEN** 文档或验证描述 virtio-blk 能力
- **THEN** 它 MUST 将其标识为首版 modern-only、单 split virtqueue、MSI-X 完成的有界块存储驱动
- **AND** 它 MUST NOT 声称 legacy/transitional、INTx 完成、多队列/packed ring、NVMe、用户态 async I/O、默认 FS 后端替换或完整 PCI 枚举/热插拔
