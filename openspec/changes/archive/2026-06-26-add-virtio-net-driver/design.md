## Context

当前 BigOS 已有内核内部设备框架、PCI 配置访问、MSI-X 投递边界、MMIO 映射、异步完成生命周期和 modern-only virtio-blk 路径。网络设备驱动可以复用这些基础，但网络设备的收包路径与块设备不同：RX 需要提前提供空包缓冲，TX 完成只表示设备已消费发送缓冲，首版还没有协议栈或 socket 等上层消费者。

本设计只建立内核内部 virtio-net 设备能力。它为后续有界协议路径提供显式设备入口，但不改变默认启动、磁盘布局、文件系统、系统调用 ABI、用户态 fd 模型或当前 userland baseline。

## Goals / Non-Goals

**Goals:**

- 实现 modern-only virtio-net PCI 设备探测、feature 协商、设备状态转换和确定性失败诊断。
- 初始化有界 RX/TX split virtqueue，使用对齐的内核内存提供 descriptor table、available ring、used ring 和包缓冲。
- 将 TX/RX used ring 完成接入 MSI-X 中断分发与 LAPIC EOI 边界，IRQ handler 保持 allocation-free、nonblocking。
- 通过通用 `bigos::device` 框架提供内核内部网络设备发布/查询接口，允许后续协议层显式取得已就绪设备。
- 提供默认关闭验证，证明设备发布、TX 完成、RX 包进入驱动缓冲、错误/timeout 和默认启动回归。

**Non-Goals:**

- 不实现 IP、ARP、ICMP、UDP、TCP、DHCP、DNS、路由、网卡配置、包过滤或完整网络栈。
- 不新增 socket syscall、用户可见 fd 类型、设备节点、mount 名称、用户态网络工具或网络 ABI。
- 不支持 legacy/transitional virtio、packed ring、多队列、RSS、offload、热插拔、IOMMU、中断重映射或跨 ISA 后端。
- 不改变 boot handoff、链接地址、页表布局、中断/系统调用向量、启动磁盘、exFAT、`/rw` 或默认 userland 启动依赖。

## Decisions

### modern-only virtio-net，拒绝 legacy 回退

驱动只接受 virtio PCI modern capability 和 `VIRTIO_F_VERSION_1`，并在缺少必需 capability、BAR 映射失败、queue 配置失败或 feature 协商失败时设置确定性失败状态。

备选方案是兼容 legacy/transitional virtio-net。该方案会引入旧 IO port 布局、不同的设备配置语义和额外验证矩阵，与当前 modern storage 路线不一致，因此不纳入首版。

### RX/TX 使用两个有界 split virtqueue

首版使用一个 RX queue 和一个 TX queue，队列深度、descriptor 数量和包缓冲数量均为编译期有界容量。RX 初始化后预投递固定数量空缓冲；TX 只接受驱动内部验证或后续协议层提交的有界 frame，提交失败必须返回确定性状态。

备选方案是建立动态缓冲池或多队列。它可以提升吞吐，但会扩大内存生命周期、锁粒度和中断并发范围。首版目标是建立正确性边界，而不是吞吐优化。

### completion handler 只回收设备完成，不执行协议语义

MSI-X handler 只读取 RX/TX used ring，完成 TX buffer 回收，记录 RX frame 元数据并唤醒或标记内核内部等待者。handler 不分配内存、不阻塞、不解析协议、不访问 VFS、不进入用户态 fd/syscall 语义，并且只走 LAPIC EOI。

备选方案是在 IRQ 中直接解析 ARP/IP 或唤醒用户 socket。该方案会把尚未定义的协议/ABI 语义耦合到驱动层，且容易破坏 IRQ-safe 约束。

### 设备发布进入通用 `bigos::device`

virtio-net 成功初始化后通过通用 `bigos::device` registry 发布为 `DeviceClass::Network` 下的内核内部网络设备接口，并使用专用网络角色作为稳定 registry key。接口暴露最小状态：MAC 地址、MTU、link/ready 状态、TX 提交入口、RX 获取/归还入口或等价有界队列观察能力。该接口不生成用户可见节点或 syscall 编号。

备选方案是先使用 driver-local 查询函数，或先设计 socket ABI 再反推驱动接口。driver-local 查询函数会降低首版改动面，但会让后续协议层再迁移一次设备发现边界；socket-first 会把尚未定义的 ABI 过早耦合到驱动。首版选择 `bigos::device`，但只发布 frame-level 网络接口，避免把协议或用户态语义固化进去。

### QEMU 验证使用 tap 后端，不改变默认启动

验证路径在支持 modern virtio-net + MSI-X 的 QEMU 配置下运行，并使用 tap 后端向 guest 提供可观察的 virtio-net 设备与 RX 包入口。本变更纳入宿主侧最小 tap 配置/清理脚本，用于创建验证所需 tap 设备、记录权限前置条件、支持注入固定测试 frame，并在退出或失败时尽量恢复宿主侧配置。验证覆盖探测、队列初始化、TX 完成和可控 RX 接收。默认启动路径不依赖 virtio-net；tap 权限、宿主网络配置、仿真器支持或工具链不可用时必须记录跳过覆盖项与残余风险。

备选方案是 socket backend、测试专用内部 producer，或只把 tap 配置作为手动前置条件记录在 `validation.md`。socket/backend producer 更可控但距离真实 virtio-net 后端更远；纯手动配置容易造成不可复现的本地状态；默认启动强依赖网卡会降低普通 boot/userland baseline 的可移植性。首版采用 tap，并把宿主侧最小配置脚本作为本变更交付物。

### 提取小型 virtio common helper

本变更提取一个小型 virtio common helper 层，承载 virtio PCI capability 解析、common cfg 访问、status transition、feature bit 协商、split queue 地址写入和 queue notify 等通用机制。virtio-net 与既有 virtio-blk 共同使用这些 helper，但块设备的请求层、缓存/写回路径和验证语义不被重构。

备选方案是 virtio-net 与 virtio-blk 各自显式实现 transport 细节。该方案降低首版重构风险，但会复制 status/feature/queue 编程逻辑，并增加后续维护成本。提取 helper 的范围必须保持窄：只抽通用 transport 机制，不抽设备语义。

## Risks / Trade-offs

- [Risk] RX 缓冲生命周期与 used ring 回收错误可能导致包缓冲被重复使用或泄漏。→ 使用固定槽位、generation 或显式状态机区分 posted、completed、owned-by-consumer、reposted，并在验证中覆盖迟到/重复完成。
- [Risk] IRQ handler 误用 allocator、锁或上层协议路径会破坏中断安全。→ 将 completion 入口限制为有界状态更新和等待者通知，任务中加入 IRQ-safety 和 reentrancy review。
- [Risk] QEMU tap 收包验证依赖宿主权限、tap 创建和网络配置，导致 smoke 不稳定。→ 本变更提供最小 tap 配置/清理脚本，将权限和平台限制显式化；成功声明只限于脚本前置条件满足且实际完成 tap-backed QEMU smoke 的环境。
- [Risk] 设备接口过早模拟完整网络栈，导致后续 socket 设计受限。→ 接口只表达 frame 收发与设备状态，不表达 IP 地址、端口、路由或 socket 语义。
- [Risk] 提取 virtio common helper 可能影响已稳定的 virtio-blk 路径。→ helper 范围限制在 transport/common cfg/split queue 机制，保留 virtio-blk 设备语义和块层集成不变，并增加默认启动与 virtio-blk 回归验证。

## Migration Plan

1. 增加新的默认关闭构建开关和驱动注册路径，默认启动不探测或不要求 virtio-net 成功。
2. 提取小型 virtio common helper，并让 virtio-blk 通过回归验证确认行为不变。
3. 在普通可阻塞内核线程中执行 PCI/virtio-net probe，完成 MMIO、MSI-X 和 virtqueue 初始化后通过 `bigos::device` 发布网络接口。
4. 增加宿主侧最小 tap 配置/清理脚本，并接入基于 QEMU tap 的默认关闭验证线程；验证通过后仍保持默认启动路径独立。
5. 回滚策略为关闭该构建开关、不运行 tap 配置脚本或不注册 virtio-net 验证设备，默认 ATA/exFAT/userland baseline 不受影响。

## Open Questions

None.
