# 有界网络协议路径

BigOS 在 frame-level `NetworkDevice` 接口之上提供内核内部有界网络协议路径。该路径处理
Ethernet II 分发、有界 ARP cache 与 pending resolution、无分片 IPv4 校验、ICMPv4
echo reply，以及仅供内核使用的 UDP datagram endpoint table。

协议 context 仅支持单接口。初始化要求已有 ready 的 frame-level 网络设备，并由内核调用方提供静态
IPv4 配置。缺少设备或配置无效时，context 会以确定性诊断保持 disabled；默认启动、storage、filesystem、`/rw`、shell 和 userland 不依赖网络可用性。

该协议模块不暴露 socket、fd 对象、syscall、`/dev` 节点、libc socket 调用、DHCP、DNS、TCP、IPv6、IP 分片重组、NAT、防火墙、动态路由或多接口路由。最小用户可见 UDP socket 接口在独立 change 中包装这个内核内部 API（见 `docs/zh/arch/syscall-entry.md`）；它仍是有界 UDP 适配层，不改变该协议模块的有界容量或非目标。

协议解析只在普通内核上下文运行。Virtio-net MSI-X handler 仍只处理 frame-level RX/TX 完成状态；protocol pump 与 UDP endpoint 操作会拒绝 IRQ/nonblocking context。RX buffer 在 frame 处理结束后 exactly-once 归还，TX 成功只来自底层 network-device transmit 结果。

验证通过默认关闭的 bounded network protocol smoke case 执行。它覆盖初始化、ARP request/reply 状态、IPv4 校验、ICMP echo、UDP bind/send/receive 和 unsupported-frame 拒绝，同时不让 normal boot 依赖宿主网络后端。

## 本机地址 Loopback 路径

IPv4 输出层对目的地址分类，为本机地址流量提供一条内核内部 loopback 路径。本机地址集合为配置的本机 IPv4 地址加上整个回环网段 `127.0.0.0/8`（不仅是 `127.0.0.1`）。当 `send_ipv4` 为本机地址目的构造好报文时，直接把报文交给 IPv4 输入分发处理，而不进行 ARP 解析或经帧级设备发送；其余目的地址保持既有 `route_destination` + ARP + 帧级设备路径不变。

本机投递把源/宿地址域归一化到 `local_ipv4`（`127.0.0.1` 视为 `local_ipv4` 的别名进入输入路径），使既有 UDP 伪首部与 IPv4 校验和逻辑无需特例即可自洽。本机 UDP datagram 复用与入站帧相同的有界 UDP endpoint RX 队列与就绪唤醒；队列满与端口未绑定返回相同的确定性状态。本机 ICMP echo-to-self 有界：一个 echo request 恰好产生一次 echo reply，reply 再进入输入路径被识别为 reply、不再生成新的 request（递归深度至多一次回声）。

本机地址路径不要求 ready 帧级设备。当 context 有有效本机 IPv4 配置但无 ready `NetworkDevice` 时，进入 loopback-only 就绪模式：本机地址 bind/send/receive 可用，而对外（非本机）发送仍返回确定性 not-ready/设备状态。未配置的默认启动不会进入该模式，因此默认启动、storage、filesystem、`/rw`、shell 与 userland 与网络路径无关。

两个 append-only 诊断计数 `loopback_delivered` 与 `loopback_dropped` 在不改动既有计数的前提下区分“本机 loopback 命中”与“对外帧级命中”。loopback 路径不新增用户可见 syscall/fd/socket ABI、无 `lo` 设备节点、无接口枚举，也不引入通用 routing/转发或多接口模型；用户程序仅通过既有 UDP socket 面向本机地址收发即可受益。验证通过默认关闭的 `loopback_network_smoke` build switch 执行，在 COM1 输出 `BIGOS_LOOPBACK_NETWORK_PASSED` / `BIGOS_LOOPBACK_NETWORK_FAILED`，覆盖 UDP loopback 闭环、ICMP echo-to-self 以及 unbound/queue-full/非本机错误路径，且无需真实 tap 或网卡。
