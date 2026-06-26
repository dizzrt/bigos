## Why

BigOS 已具备内核内部 virtio-net frame-level 设备接口，但上层仍缺少可复现、可验证的以太网/IP 基础协议路径，用户态 socket 之前没有稳定的内核通信语义承接点。
本变更面向 M11.2，建立足以支撑基础通信的有界网络协议路径，同时明确不声明完整网络栈。

## What Changes

- 新增内核内部网络协议路径，覆盖以太网帧分发、ARP/IPv4/ICMP/UDP 的最小有界处理。
- 引入通过 boot-time kernel option 提供的本机 IPv4、MAC、网关/对端信息边界，避免 DHCP、路由表和动态网络配置依赖。
- 提供内核内部 UDP datagram 收发接口，供后续最小 socket/fd/syscall change 复用，但本变更不暴露用户态 socket ABI。
- 将协议处理与 virtio-net IRQ 完成路径解耦：中断路径只完成 frame RX/TX 状态，协议解析在普通内核上下文或有界轮询/工作路径中完成。
- 增加默认关闭的协议路径验证，覆盖 ARP 解析、IPv4 校验、ICMP echo、UDP 收发、错误丢弃和默认启动不依赖网络。
- 不引入 TCP、DNS、DHCP、IPv6、NAT、防火墙、多网卡路由、异步用户 API、完整 POSIX socket 或通用网络配置工具。

## Capabilities

### New Capabilities

- `bounded-network-protocol-path`: 定义 BigOS 内核内部有界网络协议路径，覆盖以太网、ARP、IPv4、ICMP echo、UDP datagram、协议队列/超时、错误处理和默认关闭验证边界。

### Modified Capabilities

- 无。

## Impact

- 影响内核网络协议子系统，预计新增或扩展 `kernel/core/net`、对应公开内核头、构建配置和默认关闭 smoke 入口。
- 依赖既有 `virtio-net-driver` 发布的 frame-level `bigos::device::NetworkDevice`，但不改变该驱动的需求边界。
- 复用现有 virtio-net host 辅助的 QEMU/tap 管理能力，并新增协议级受控包注入/断言验证路径；验证不可用时必须记录跳过原因和剩余风险。
- 不改变启动地址、链接地址、页表布局、磁盘布局、IDT/syscall vector、现有 fd/VFS/syscall ABI、默认用户态程序或默认启动依赖。
- 架构假设为当前 x86_64 freestanding 内核；内存使用必须通过显式内核分配路径或有界静态缓冲；工具链仍以 xmake 与 x86_64-elf GCC 为准，辅助 Python 验证需通过 `uv run ...`。
