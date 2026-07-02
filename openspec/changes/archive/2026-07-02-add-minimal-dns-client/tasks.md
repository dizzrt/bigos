## 1. API 与边界确认

- [x] 1.1 在 `include/bigos/errno.h` 与用户态 errno mirror 中以 append-only 方式补齐 `ETIMEDOUT=110`，并更新/补充 kernel-user errno 一致性检查。
- [x] 1.2 新增 freestanding-safe 的 BigOS-specific DNS resolver 公共头，提供 `bigos_dns_resolve_ipv4` 风格接口，不声明完整 POSIX resolver 行为。
- [x] 1.3 新增最小 `netdb.h` 兼容入口，只暴露 BigOS 有界 IPv4 A 记录解析子集，不声明 unsupported POSIX resolver 数据库或完整 `getaddrinfo` 行为。
- [x] 1.4 确认 resolver API 使用 host-order IPv4、调用者提供输出数组、显式 DNS server IPv4 和有界 timeout 参数。
- [x] 1.5 确认新增头文件不会依赖 hosted libc、线程、locale、动态初始化、动态分配或未实现 POSIX `netdb` 结构。

## 2. DNS 报文编解码

- [x] 2.1 实现 DNS query 构造：单 question、QTYPE=A、QCLASS=IN、bounded transaction ID、hostname/label/message 长度检查。
- [x] 2.2 实现 hostname 校验与 label 编码，覆盖空名、空 label、超长 label、超长总名和非法字符的拒绝路径。
- [x] 2.3 实现 DNS response header/question/answer 解析，校验 transaction ID、QR/opcode/rcode、question 匹配、RR 边界和 RDATA 长度。
- [x] 2.4 实现 DNS name compression 指针解析，检测越界、循环和过深跳转。
- [x] 2.5 实现 A/IN answer 提取到 caller-provided host-order IPv4 输出数组，并跳过不支持 RR 类型而不暴露为成功结果。
- [x] 2.6 实现 NXDOMAIN、无 A 记录、TC bit、malformed/truncated response、容量不足、超时/无响应等错误映射，其中超时/无响应必须设置 `ETIMEDOUT`。

## 3. UDP Socket 集成

- [x] 3.1 在用户态 libc resolver 中通过现有 `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)`、`sendto`、`recvfrom` 发送和接收 DNS UDP datagram。
- [x] 3.2 确保 resolver 成功和所有失败路径都关闭自身创建的 UDP socket fd，不泄漏 fd 或 UDP endpoint。
- [x] 3.3 将 UDP/socket 的 no route、not ready、would-block/no-data、queue full、地址非法和发送失败映射到 resolver 文档化 errno。
- [x] 3.4 保持现有 socket syscall number、datagram `read`/`write` 不支持语义、stream socket 语义和 fd 生命周期不变。

## 4. 验证

- [x] 4.1 增加 DNS 编解码的 source-level 或用户态 smoke 覆盖：有效 query、有效 A response、compression、非法 hostname、malformed response、无 A 记录、容量不足。
- [x] 4.2 增加默认关闭 DNS resolver smoke，使用 loopback 或注入式 UDP DNS responder 验证一次真实 DNS wire-format query/response 闭环。
- [x] 4.3 验证 DNS 超时或无响应路径返回 `ETIMEDOUT`，并确认不会泄漏 socket fd。
- [x] 4.4 运行或记录无法运行的最窄相关构建验证：xmake 交叉构建用户态 libc 与受影响用户程序。
- [x] 4.5 若实现触及内核 C++ 网络/socket 路径，运行或记录 freestanding C++17 clang/clangd 辅助检查，并区分历史诊断与当前变更诊断。
- [x] 4.6 运行或记录无法运行的默认关闭启动/用户态 smoke，确认无 DNS 配置时默认 boot、filesystem、shell 和现有 socket 能力不回归。

## 5. 收尾

- [x] 5.1 更新必要的用户态头文件说明或示例，明确 DNS resolver 是 BigOS 有界 IPv4 A 记录子集。
- [x] 5.2 更新 `roadmap.md` 中 Task M14.4 的完成状态，仅保留规划级描述，不加入实现文件路径、命令或验证 marker。
- [x] 5.3 运行 `openspec validate add-minimal-dns-client --strict` 并修复当前 change 引入的 OpenSpec 结构问题。
- [x] 5.4 记录最终验证结果，区分已通过、因工具链或网络环境无法运行、历史诊断和当前变更残余风险。
