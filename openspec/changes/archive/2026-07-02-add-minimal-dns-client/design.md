## Context

BigOS 当前网络能力已经覆盖静态 IPv4/ARP/ICMP/UDP、loopback、UDP datagram socket，以及 TCP stream socket 与 fd/poll/syscall 的集成。M14.4 的缺口不是再扩展传输层，而是让简单用户程序可以把域名解析为 IPv4 地址，然后复用现有 TCP/UDP socket 连接目标。

当前约束：
- 内核网络协议路径保持有界、非 IRQ 协议处理、默认关闭验证。
- 用户态 libc 是 freestanding 最小子集，不能依赖 hosted resolver、线程、locale、动态初始化或宿主系统头。
- `sys/socket.h` 只承诺 BigOS 有界 IPv4 UDP/TCP 子集，地址结构为 host-order `sockaddr_in`。
- 默认启动不得依赖 DNS server、tap 权限、外部网络或真实 nameserver。

## Goals / Non-Goals

**Goals:**
- 提供最小 DNS client，支持 UDP/53 上的标准 DNS query/response，解析 IPv4 A 记录。
- 在用户态 libc 暴露有界名字解析 API，使简单 C 程序可把域名转换为 host-order IPv4。
- 复用既有 UDP socket syscall、用户缓冲校验、fd 生命周期、errno 翻译和有界等待语义。
- 提供默认关闭验证，覆盖成功解析、格式错误、超时、截断、无 A 记录和容量边界。

**Non-Goals:**
- 不实现完整 POSIX resolver、`getaddrinfo` 全矩阵、`/etc/resolv.conf`、缓存 daemon 或后台服务。
- 不实现递归 resolver、DNSSEC、EDNS、TCP fallback、IPv6/AAAA、CNAME 链跟随、搜索域、国际化域名、多 nameserver 重试策略。
- 不新增内核 DNS syscall，不把 DNS 解析器放入常驻内核协议路径。
- 不改变 boot/kernel ABI、syscall vector、页表布局、磁盘布局、UEFI/Legacy 启动协议或已有 socket syscall 编号。

## Decisions

1. DNS client 放在用户态 libc，而不是内核协议层。

   用户态实现可以直接复用现有 `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)`、`sendto`、`recvfrom` 与 errno 翻译路径，避免新增 syscall 和内核常驻状态。内核仍只负责 UDP datagram 传输、fd 生命周期与有界等待。

   备选方案是内核提供 `resolve(name)` syscall。该方案会把 DNS 报文解析、配置、超时和潜在缓存状态引入内核 ABI，和 M14.4 “基于既有 UDP 路径”的最小目标不匹配。

2. 公共 API 同时提供 BigOS-specific IPv4 resolver 与最小 `netdb.h` 兼容入口，但不承诺完整 `getaddrinfo`。

   暴露 `bigos_dns_resolve_ipv4(name, dns_server_ipv4, out_addrs, capacity, timeout_ticks)` 这类显式有界接口：调用者传入 DNS server IPv4、输出数组容量和超时上界，返回解析出的 A 记录数量或 `-1` 并设置 `errno`。同时提供最小 `netdb.h` 兼容入口，用于让简单程序通过熟悉的头文件获得 BigOS 有界 IPv4 A 记录解析能力；该头文件只能声明本变更实际实现的子集，不得暗示完整 POSIX resolver 支持。

   备选方案是直接实现 `getaddrinfo`。该接口会牵连 service name、hints、family/type/protocol 组合、链表分配、释放函数和大量 POSIX 兼容承诺，超出当前 libc 边界。

3. DNS wire format 只支持单问题 A 查询和单响应解析。

   Query 使用单个 question：QTYPE=A、QCLASS=IN，域名 label 长度与总报文长度使用编译期上界。Response 必须匹配 transaction ID、QR=response、RCODE=0、question 与请求一致，并在 answer 区解析 A/IN 记录；允许跳过不支持的 RR，支持常见 name compression 指针，但必须检测压缩指针循环和越界。

   备选方案是不支持 compression。真实 DNS 响应通常在 answer name 使用压缩指针，完全拒绝 compression 会让能力在真实 nameserver 上过窄。

4. DNS server 配置保持显式，不引入通用配置数据库。

   首期解析入口优先要求调用者显式传入 `dns_server_ipv4`。如需要从环境读取，只能作为用户态 helper 读取受限环境变量，例如 `BIGOS_DNS_SERVER` 的 dotted-quad 值；不得引入 `/etc/resolv.conf`、DHCP、网络配置 daemon 或内核全局 nameserver 状态。

5. 错误语义映射到现有 errno，不新增 DNS 专用 syscall 错误域。

   当前统一 errno 集缺少 `ETIMEDOUT`。本变更实现期必须在内核单一 errno 来源与用户态 mirror 中补齐 `ETIMEDOUT`（采用常规 POSIX/Linux 值 110）并保持 source-contract 校验一致。Malformed input 使用 `EINVAL`，输出容量不足使用 `ERANGE`，无 A 记录或 NXDOMAIN 使用确定性 `ENOENT`，超时/无响应使用 `ETIMEDOUT`，socket/UDP 发送接收错误沿用已有 wrapper 的 errno。

## Risks / Trade-offs

- [Risk] 用户态 resolver 复用 UDP socket 时会消耗 fd 与 UDP endpoint 容量。→ Mitigation: 每次调用创建/关闭一个 socket 或复用调用者显式传入资源的后续扩展；失败路径必须关闭 fd。
- [Risk] 真实 DNS 响应格式多样，过窄解析器可能拒绝合法响应。→ Mitigation: 首期只承诺 A/IN、单问题、常见 compression；其他 RR 可跳过但不扩展语义。
- [Risk] 未实现 TCP fallback，超过 UDP 报文或 TC bit 响应会失败。→ Mitigation: 明确把 TC bit 映射为确定性失败，不声称完整 resolver。
- [Risk] 缺少真实网络环境会影响验证。→ Mitigation: 默认关闭 smoke 提供本机注入式 DNS server 或 loopback UDP 闭环；真实 nameserver 验证只能作为环境具备时的补充。
- [Risk] dotted-quad 字符串解析和域名编码容易越界。→ Mitigation: 所有 label、报文、输出数组和字符串长度使用编译期上界，解析前做完整边界检查。

## Migration Plan

1. 补齐统一 `ETIMEDOUT` errno，并增加用户态 resolver 头文件、最小 `netdb.h` 兼容入口与 libc 实现，确保不影响现有 socket wrapper 和默认启动。
2. 补齐 DNS 报文编码/解析的 source-level 单元检查或 smoke helper。
3. 增加默认关闭用户态/内核配合 smoke，使用 loopback UDP 闭环模拟 DNS server。
4. 扩展示例用户程序或 smoke，使一个域名解析为 IPv4 后可用于现有 socket 地址结构。
5. 若实现失败或需要回滚，移除新增 resolver 源文件/头文件和 gated smoke，不改变内核 syscall ABI。

## Open Questions

- 无。
