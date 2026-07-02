## Why

M14 已经具备 loopback、UDP、TCP stream socket 与 fd/multiplexing 集成，但用户程序仍只能直接使用数值 IPv4 地址，无法完成最基础的名字到 IPv4 地址解析。M14.4 需要在既有 UDP 路径之上补齐一个有界 DNS client，使简单 TCP/UDP client 能通过域名连接目标，同时保持 BigOS 当前“有界能力”原则。

## What Changes

- 新增最小 DNS client 能力，支持通过 UDP/53 发送单个 DNS query，并解析基础 IPv4 A 记录响应。
- 在用户态 libc 暴露 BigOS 有界名字解析入口与最小 `netdb.h` 兼容入口，供简单 C 程序把域名解析为 host-order IPv4 地址。
- 复用现有 UDP socket syscall、fd 生命周期、非阻塞/有界等待与 errno 翻译路径，不新增内核 DNS syscall。
- 以 append-only 方式补齐统一 `ETIMEDOUT` errno，使 DNS 超时/无响应有明确错误语义。
- 增加 DNS server 配置边界：解析调用必须显式传入 DNS server IPv4，或通过受限的 BigOS 用户态配置入口读取；不引入通用网络配置数据库。
- 增加默认关闭验证，覆盖 DNS query 构造、response 解析、超时/格式错误/截断/无 A 记录等失败路径。
- 非目标：不实现完整 POSIX resolver、`/etc/resolv.conf`、缓存 daemon、递归解析器、IPv6/AAAA、CNAME 链跟随、TCP fallback、EDNS、DNSSEC、搜索域、国际化域名或多 nameserver 策略。

## Capabilities

### New Capabilities
- `minimal-dns-client`: 定义 BigOS 最小 DNS client 的查询、响应解析、用户态 resolver API、错误语义、配置边界与验证要求。

### Modified Capabilities
- `user-libc-min`: 增加 freestanding-safe 的最小名字解析声明与 wrapper/errno 边界，不扩大为完整 hosted/POSIX libc。
- `minimal-socket-interface`: 约束 DNS client 复用既有 UDP socket 能力，并保持 datagram socket 的 fd、sendto/recvfrom 与错误映射语义不变。
- `bounded-network-protocol-path`: 明确 DNS 流量只是 UDP payload 的上层消费者，不改变 IPv4/UDP 协议路径、ARP、loopback 或 IRQ 边界。
- `unified-errno`: 以 append-only 方式新增 `ETIMEDOUT`，并要求内核单一 errno 来源与用户态 mirror 保持一致。

## Impact

- 受影响子系统：用户态 libc、用户态 socket 头文件/网络头文件、UDP socket syscall 使用路径、网络协议默认关闭 smoke、用户程序构建包装。
- 内核影响：不新增 DNS syscall；仅在必要时补齐 UDP socket 可复用性或测试注入辅助，不改变 boot/kernel ABI、内存布局、磁盘布局、IDT/syscall vector、UEFI/Legacy 启动边界。
- 用户态影响：新增有界名字解析 API、最小 `netdb.h` 兼容入口与 BigOS-specific resolver 头；简单用户程序可解析单个域名得到 IPv4 地址。
- 架构与工具链假设：继续限定 x86_64 freestanding C/C++17、现有 xmake 构建、x86_64-elf-gcc、QEMU/Bochs 验证边界；默认启动不依赖 DNS server 或真实网络后端。
