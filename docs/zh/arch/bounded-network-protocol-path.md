# 有界网络协议路径

BigOS 在 frame-level `NetworkDevice` 接口之上提供内核内部有界网络协议路径。该路径处理
Ethernet II 分发、有界 ARP cache 与 pending resolution、无分片 IPv4 校验、ICMPv4
echo reply，以及仅供内核使用的 UDP datagram endpoint table。

协议 context 仅支持单接口。初始化要求已有 ready 的 frame-level 网络设备，并由内核调用方提供静态
IPv4 配置。缺少设备或配置无效时，context 会以确定性诊断保持 disabled；默认启动、storage、filesystem、`/rw`、shell 和 userland 不依赖网络可用性。

该协议模块不暴露 socket、fd 对象、syscall、`/dev` 节点、libc socket 调用、DHCP、DNS、TCP、IPv6、IP 分片重组、NAT、防火墙、动态路由或多接口路由。最小用户可见 UDP socket 接口在独立 change 中包装这个内核内部 API（见 `docs/zh/arch/syscall-entry.md`）；它仍是有界 UDP 适配层，不改变该协议模块的有界容量或非目标。

协议解析只在普通内核上下文运行。Virtio-net MSI-X handler 仍只处理 frame-level RX/TX 完成状态；protocol pump 与 UDP endpoint 操作会拒绝 IRQ/nonblocking context。RX buffer 在 frame 处理结束后 exactly-once 归还，TX 成功只来自底层 network-device transmit 结果。

验证通过默认关闭的 bounded network protocol smoke case 执行。它覆盖初始化、ARP request/reply 状态、IPv4 校验、ICMP echo、UDP bind/send/receive 和 unsupported-frame 拒绝，同时不让 normal boot 依赖宿主网络后端。
