## Context

BigOS 当前已经有 modern-only virtio-net 驱动规格和内核内部 frame-level `NetworkDevice` 接口。该接口能发布设备、提交/接收以太网帧并完成默认关闭验证，但它刻意不解析 ARP、IPv4、ICMP、UDP，也不创建 socket/fd/syscall ABI。

M11.2 的目标是在 M11.1 设备能力和 M11.3 用户可见 socket 之间补齐一层可验证的内核内部协议路径。该路径必须保持 freestanding-safe、容量有界、错误可诊断，且默认启动不依赖网络设备或宿主网络配置。

本变更影响内核网络协议子系统、构建开关、默认关闭 smoke，以及可能的宿主侧 QEMU/tap 验证辅助。它不改变启动地址、链接地址、页表布局、磁盘布局、中断向量、syscall number、fd/VFS 行为或用户态 ABI。

## Goals / Non-Goals

**Goals:**

- 建立内核内部有界网络协议路径，覆盖以太网帧分发、ARP、IPv4、ICMP echo 和 UDP datagram。
- 将协议解析从 virtio-net MSI-X IRQ 完成路径中分离，保持 IRQ 路径只更新设备 RX/TX 状态。
- 通过 boot-time kernel option 提供本机 IPv4/MAC/网关或直连对端边界，不依赖 DHCP、DNS 或动态路由。
- 提供内核内部 UDP datagram 收发端口接口，供后续 socket/fd/syscall change 包装。
- 用默认关闭 smoke 验证 ARP、IPv4、ICMP、UDP、错误丢弃和默认启动独立性。

**Non-Goals:**

- 不实现 TCP、IPv6、DHCP、DNS、ICMP 全量语义、IP 分片重组、NAT、防火墙、多网卡路由或动态路由表。
- 不暴露用户态 socket API、fd 类型、syscall、`/dev` 节点、网络配置命令或 libc socket 函数。
- 不改变 virtio-net driver 的 modern-only、frame-level、内核内部设备边界。
- 不引入后台无限队列、动态线程池、hosted runtime 依赖、异常、RTTI 或非 freestanding 库。

## Decisions

1. 协议层新增为内核内部 `bigos::net` 能力，而不是扩展 virtio-net driver。

   选择该方案是为了保持设备驱动只处理 frame RX/TX、queue ownership 和 MSI-X 完成，协议层负责以太网/ARP/IP/ICMP/UDP 状态。备选方案是直接在 virtio-net driver 中解析协议，但这会把设备状态机、IRQ 完成和协议语义耦合，后续 socket 层也难以复用。

2. 初期只支持单网络接口，并通过 boot-time kernel option 提供静态 IPv4 配置。

   这让协议路径能在 smoke 和后续用户可见 socket 之前复用同一套启动期配置来源，同时避免 DHCP、路由表、多网卡选择和地址生命周期管理。备选方案包括编译期常量和 smoke-only 配置结构；编译期常量会让验证环境变更必须重新构建，smoke-only 配置又难以服务后续 M11.3 的最小 socket 包装。通用网络配置模型暂不采用，因为它会把 M11.2 扩张到完整网络栈方向，超出当前里程碑边界。

3. 协议处理运行在普通内核上下文或显式 smoke 轮询路径，不在 IRQ handler 中执行。

   数据流为：virtio-net IRQ handler 消费 used ring 并标记 RX frame 可用；协议 pump 在非 IRQ 上下文从 `NetworkDevice::poll_rx` 拉取 frame；以太网分发后交给 ARP/IPv4 处理；需要发送时构造 bounded frame 并调用 `NetworkDevice::transmit`；处理完成后归还 RX buffer。这样 IRQ 路径不分配、不阻塞、不访问 VFS、不解析协议。

4. 使用有界表和队列表达协议状态。

   ARP cache、pending ARP 请求、UDP 端口绑定表、每端口 RX datagram 队列、临时包缓冲和诊断计数都使用固定容量或初始化期显式分配容量。满载时返回确定性错误或丢弃并记录诊断，不进行无界增长。备选方案是按需分配每个 packet/endpoint，但在内核早期网络路径中更难保证失败行为和 IRQ 边界。

5. IPv4 只接受无分片或完整单包 datagram。

   初期不实现 IP 分片重组；带 fragment offset 或 `MF` 标志的数据包必须被丢弃并计数。这样可以避免复杂的超时重组缓存和内存放大风险。后续若需要，可独立提出 file-backed 或 bounded reassembly change。

6. UDP API 只提供内核内部 endpoint 语义。

   端口绑定、发送到 IPv4:port、接收 datagram、超时/无数据/队列满等结果通过内核内部结构返回。M11.3 可以在此之上包装 fd/syscall，但本变更不承诺 POSIX socket 行为、blocking flag、poll/select、errno 映射或用户缓冲复制。

7. 验证采用默认关闭 smoke，默认启动保持无网络依赖，并复用现有 virtio-net host 辅助的 QEMU/tap 管理能力。

   smoke 在可用环境中复用现有 virtio-net host 辅助完成 QEMU/tap 启动、清理和基础设备挂接，再增加协议级受控包注入与断言逻辑，验证 ARP 解析、ICMP echo、UDP 闭环和错误路径。备选方案是复制一套协议 smoke 专用 QEMU/tap 管理工具，但这会增加宿主网络状态清理和启动参数维护成本。若 QEMU、tap 权限、MSI-X、串口捕获或交叉工具链不可用，验证记录跳过项和剩余风险，不声称运行成功。

## Risks / Trade-offs

- [Risk] boot-time 静态 IPv4 配置降低通用性 -> Mitigation: 将其明确为 M11.2 的有界边界，后续 DHCP/配置工具单独建 change。
- [Risk] 单接口模型会影响未来多网卡扩展 -> Mitigation: 对外协议 API 保留显式 interface/context 参数，但实现只接受一个 ready network device。
- [Risk] 无 IP 分片重组导致部分真实流量不可用 -> Mitigation: smoke 只依赖无分片包，规格要求对分片包确定性丢弃并诊断。
- [Risk] 协议 pump 若被放入不合适的初始化阶段，可能早于 virtio-net 发布运行 -> Mitigation: 初始化必须检查 network device ready 状态，缺失设备时进入 disabled/skipped 状态而不影响默认启动。
- [Risk] QEMU/tap 验证依赖宿主权限和网络后端 -> Mitigation: tasks 要求记录不可运行原因，默认启动回归仍可在无网络环境中验证。

## Migration Plan

1. 新增 `bigos::net` 内核内部协议模块和最小公开头，默认配置下可编译但不改变用户态 ABI。
2. 接入既有 `bigos::device::NetworkDevice`，在设备不存在或未 ready 时保持协议层 disabled。
3. 增加默认关闭的 build switch 和 smoke 入口，用受控以太网帧验证协议路径。
4. 在完成验证后，将 roadmap 的 M11.2 标记为已完成；不归档或修改 M11.3 范围。

Rollback 策略：关闭新增 build switch 或移除协议模块注册即可恢复到仅有 virtio-net frame-level 设备能力；由于不改变 syscall/fd/boot ABI，回滚不需要用户态迁移。

## Open Questions

- 无。
