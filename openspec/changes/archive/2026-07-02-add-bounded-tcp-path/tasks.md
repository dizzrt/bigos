# add-bounded-tcp-path 任务清单

按子系统拆分，依赖顺序自上而下：先在网络协议头补齐 TCP 协议号常量、TCP 相关容量常量与 `Diagnostics` 末尾 TCP 计数字段；再新增独立 TCP 状态机单元（`kernel/core/net/tcp.cc` + `include/bigos/net/tcp.h`）实现 TCB 池、状态机、连接建立、有序交付/重组、有界重传/窗口、连接拆除与有界 `TIME_WAIT` 回收；随后在 `kernel/core/net/protocol.cc` 的 `handle_ipv4` 追加协议号 6 分支并经既有 `send_ipv4` 输出 TCP 段；最后补 smoke、构建/静态检查、源级契约与仿真器验证、文档与验证记录。改动集中在 `include/bigos/net.h`、`include/bigos/net/tcp.h`、`kernel/core/net/tcp.cc`、`kernel/core/net/protocol.cc`（C++），涉及 `xmake/` 构建开关与 `kernel/core/kernel.cc` smoke 入口。新增枚举/字段/常量一律末尾追加，不重排既有布局，并以 `static_assert` 守护关键既有结构（如 `Diagnostics`）不受影响。不新增或改动任何 syscall/socket ABI，不暴露 `connect`/`listen`/`accept` 用户接口，不改动 boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。

## 1. 网络协议头：TCP 常量与诊断计数

- [x] 1.1 在 `include/bigos/net.h` 增补 `constexpr uint8_t IPV4_PROTOCOL_TCP = 6;`（置于既有 `IPV4_PROTOCOL_ICMP`/`IPV4_PROTOCOL_UDP` 常量附近的末尾，不改动既有常量取值）。
- [x] 1.2 在 `include/bigos/net.h` 或 `include/bigos/net/tcp.h` 定义 TCP 有界容量与计时常量：`TCP_CONNECTION_CAPACITY`（定容 TCB 池大小）、`TCP_MSS`（TCP 数据段最大字节，取值使 `20(IP)+20(TCP)+TCP_MSS` 不超过既有 `send_ipv4` 栈缓冲 `20 + UDP_MAX_PAYLOAD + 8` 与 `mtu`）、`TCP_SEND_BUFFER`/`TCP_RECV_BUFFER`（定容发送/接收缓冲）、`TCP_RETX_QUEUE_CAPACITY`（定容重传队列）、`TCP_REORDER_SLOTS`（定容乱序重组槽）、`TCP_RTO_MIN`（RTO 下限，对齐 Linux 200ms => 20 ticks）、`TCP_RTO_MAX`（RTO 上限，对齐 Linux 120s => 12000 ticks）、`TCP_MSL_TICKS`（MSL，tick 单位；`TIME_WAIT` 时长 = `2*TCP_MSL_TICKS`）、`TCP_MAX_RETRANSMIT`（有界最大重传次数）。以编译期 `static_assert` 守护 `20 + 20 + TCP_MSS <= 20 + UDP_MAX_PAYLOAD + 8` 与 `<= DEFAULT_MTU` 的关系，确保 TCP 段不越过既有 IPv4 载荷缓冲上界。
- [x] 1.3 在 `Diagnostics` 末尾（当前 `loopback_dropped` 之后、`last_status` 之前）append-only 追加 TCP 计数字段：至少 `tcp_segments_rx`、`tcp_segments_tx`、`tcp_retransmits`、`tcp_connections_opened`、`tcp_connections_closed`、`tcp_resets`、`tcp_dropped`。新增字段一律末尾追加，并以 `__builtin_offsetof` `static_assert` 守护既有字段（含 `loopback_delivered`/`loopback_dropped`）偏移与 `last_status` 相对位置不变；不改动既有计数取值语义。

## 2. TCP 状态机单元：TCB 池与结构

- [x] 2.1 新增 `include/bigos/net/tcp.h`，声明 `enum class TcpState`（`Closed`/`Listen`/`SynSent`/`SynReceived`/`Established`/`FinWait1`/`FinWait2`/`Closing`/`CloseWait`/`LastAck`/`TimeWait`）与 `struct TcpControlBlock`（四元组本地/远端 `Ipv4Address`+port、`TcpState`、发送侧 `snd_una`/`snd_nxt`/`snd_wnd` + 定容发送缓冲 + 定容重传队列（每未确认段含 `send_tick`/`rto_deadline_tick`/`retransmit_count`/`retransmitted` 标志）、RFC 6298 估计器状态 `srtt`/`rttvar`/`rto`（tick 单位整数）、接收侧 `rcv_nxt`/`rcv_wnd` + 定容接收缓冲 + 定容乱序重组槽、`time_wait_deadline_tick`、`active` 标志），保持公共头精简、仅包含所需依赖。声明内核内部 API：连接主动打开、被动 `LISTEN`、发送数据、接收数据、关闭、以及 `handle_tcp(ctx, source, dest, segment, len)` 与超时/重传推进入口 `tcp_pump(ctx)` 或等价。
- [x] 2.2 新增 `kernel/core/net/tcp.cc`，实现定容 TCB 池（`TCP_CONNECTION_CAPACITY`）、按四元组精确匹配的连接查找、按本地端口匹配的 `LISTEN` 查找、TCB 分配（无空闲槽时确定性 `TableFull` + `tcp_dropped` 递增）与回收（释放定容缓冲/重组槽、清零复用）。实现 32 位回绕安全序号比较辅助（有符号差值语义），仅在代码注释记录该非显然约束。

## 3. TCP 连接建立

- [x] 3.1 在 `tcp.cc` 实现主动打开：分配 TCB、初始化本地初始序号、经 `send_ipv4(ctx, dest, IPV4_PROTOCOL_TCP, segment, len)` 发送 SYN、进入 `SynSent`；收到匹配 SYN,ACK 时以对端初始序号初始化 `rcv_nxt`、发送 ACK、进入 `Established`，递增 `tcp_connections_opened`。
- [x] 3.2 在 `tcp.cc` 实现被动打开：`LISTEN` TCB 收到匹配 SYN 时分配/初始化连接、发送 SYN,ACK、进入 `SynReceived`；收到匹配 ACK 后进入 `Established`，递增 `tcp_connections_opened`。
- [x] 3.3 处理非法/不匹配握手段：序号/确认号与状态不匹配、标志组合非法或校验和失败时按状态确定性丢弃或发送 RST，不迁移到 `Established`，递增 `tcp_dropped`/`tcp_resets`。

## 4. 有序数据交付、重组与累积 ACK

- [x] 4.1 在 `tcp.cc` 实现按序接收：序号等于 `rcv_nxt` 且落在接收窗口内的数据写入定容接收缓冲、推进 `rcv_nxt`、回送累积 ACK；提供内核内部按序读取接口。
- [x] 4.2 实现有界乱序重组：序号高于 `rcv_nxt` 但落在接收窗口内的段缓存进定容乱序重组槽（`TCP_REORDER_SLOTS`），缺口被按序段填补后按序合并推进 `rcv_nxt`；重组槽满时对超额乱序段确定性丢弃（`tcp_dropped` 递增），不无界缓冲。
- [x] 4.3 实现重复/超窗/越界/校验失败段的确定性丢弃并递增 `tcp_dropped`，不写入未校验数据、不破坏已交付有序数据。
- [x] 4.4 实现累积 ACK 推进：推进 `snd_una`、从定容重传队列回收已确认段与其重传计时；过期/重复 ACK 不当作新确认破坏发送窗口。

## 5. RFC 6298 动态重传与窗口流控

- [x] 5.1 在 `tcp.cc` 实现重传登记：每个未确认段登记进定容重传队列，记录 `send_tick`，并按当前 `rto` 设置 `rto_deadline_tick`（基于 `timer::ticks()`）；初始 `rto` 在有 RTT 采样前取 `TCP_RTO_MIN` 或 RFC 6298 建议初值并 clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`。
- [x] 5.2 实现 RFC 6298 RTT 估计器：对未被重传过的段被累积 ACK 确认时采样 RTT `R = now - send_tick`；首次 `srtt = R`、`rttvar = R/2`，后续 `rttvar = rttvar - (rttvar>>2) + (|srtt-R|>>2)`、`srtt = srtt - (srtt>>3) + (R>>3)`（alpha=1/8、beta=1/4 移位实现，全整数无浮点）；`rto = srtt + max(G, 4*rttvar)`（G=1 tick），clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`。遵循 Karn 算法：`retransmitted` 标志为真的段不参与 RTT 采样。
- [x] 5.3 实现 `tcp_pump`/超时推进（ordinary 上下文，沿用 ARP pending 式 tick 驱动）：检查到期未确认段并重传、置 `retransmitted`、按指数退避 `rto = min(rto*2, TCP_RTO_MAX)` 更新 `rto_deadline_tick`、递增 `retransmit_count` 与 `tcp_retransmits`；退避不改写 `srtt`/`rttvar`。重传次数超过 `TCP_MAX_RETRANSMIT` 时确定性复位/放弃连接并回收 TCB，递增 `tcp_resets`。
- [x] 5.4 实现累积 ACK 与估计器联动：ACK 推进 `snd_una`、从重传队列回收已确认段；对未重传段用其 `send_tick` 触发 5.2 的 RTT 采样与 `rto` 重算。
- [x] 5.5 实现通告窗口流控：以接收缓冲剩余空间作为通告窗口回送对端，接收缓冲满时通告有界（含 0）窗口，不接受超过通告窗口的数据、不无界缓冲。
- [x] 5.6 复核上下文边界：TCP 发送、`handle_tcp`、`tcp_pump`、RTT 采样与唤醒复用既有 ordinary-context 边界（`ordinary_context()`/`UnsupportedContext`）；MUST NOT 从 IRQ 上下文触发重传、RTT 采样、分配或操作 TCB 缓冲。在代码注释记录该边界约束。

## 6. 连接拆除与标准 TIME_WAIT

- [x] 6.1 在 `tcp.cc` 实现正常关闭：发送 FIN 进入 `FinWait1`，按对端 ACK/FIN 迁移到 `FinWait2`/`TimeWait`，递增 `tcp_connections_closed`。
- [x] 6.2 实现被动关闭（收 FIN → `CloseWait` → 本地关闭 → `LastAck`）与同时关闭（`Closing`），完成后回收 TCB 与其定容缓冲/重组槽。
- [x] 6.3 实现标准 `TIME_WAIT`：进入 `TimeWait` 设置 `time_wait_deadline_tick = timer::ticks() + 2*TCP_MSL_TICKS`（标准 `2*MSL`，对齐 Linux）；`tcp_pump` 中到期回收 TCB 使槽位可复用；`TIME_WAIT` 槽占满时新建连接返回确定性 `TableFull`，不提前回收 `TimeWait` TCB。在代码注释记录「标准 `2*MSL` `TIME_WAIT`」的语义约束。
- [x] 6.4 实现 RST/异常复位：收到匹配 RST、重传超限或不可恢复非法段时确定性复位并立即回收 TCB，递增 `tcp_resets`，不泄漏定容缓冲。

## 7. 协议层挂接（IPv4 输入/输出）

- [x] 7.1 在 `kernel/core/net/protocol.cc` 的 `handle_ipv4` 协议号分发追加 TCP 分支：`protocol == IPV4_PROTOCOL_TCP` 时调用 `handle_tcp(ctx, source, dest, body, body_len)`（含前向声明与头包含），保持既有 ICMP/UDP 分发、`ipv4_unsupported_protocol` 语义与本机过滤（`is_local_delivery(ctx,dest) || BROADCAST`）不变；递增 `tcp_segments_rx`。
- [x] 7.2 确认 TCP 段输出复用既有 `send_ipv4`：本机地址目的经既有 loopback 分流（宿地址归一化为 `local_ipv4`）、对外目的经既有 `route_destination` + ARP + 帧级设备（无 ready 设备时 `NotReady`）；`tcp.cc` 输出侧递增 `tcp_segments_tx`。不为 TCP 另建第二套 IPv4 输出/校验逻辑。
- [x] 7.3 实现 TCP 校验和：IPv4 伪首部（源/宿 IPv4、协议号 6、TCP 长度）+ TCP 头 + 数据；本机地址闭环时伪首部宿地址域与 `send_ipv4` 归一化一致（`local_ipv4`），使 `handle_tcp` 重建校验和自洽，无需特例。以本机 TCP send→receive 校验和通过为准绳验证归一化正确。
- [x] 7.4 复核默认启动独立性：TCP 仅由显式内核内部调用或 smoke 驱动；默认 `g_default_context` 保持 `Disabled`，normal boot 不初始化 TCP；`handle_ipv4` 的 TCP 分支仅在有对应连接/`LISTEN` 时产生状态迁移，无匹配时确定性丢弃，不影响既有 UDP/ICMP 与默认 boot。

## 8. 运行期 smoke 验证

- [x] 8.1 在 `xmake/options.lua` 新增默认关闭开关 `tcp_path_smoke`（`set_default(false)`、`set_showmenu(true)`、描述其为 validation-only 内核内部有界 TCP 本机地址闭环 marker），遵循既有 `loopback_network_smoke`/`network_protocol_smoke` 选项模式。
- [x] 8.2 在 `xmake/kernel.lua` 把 `tcp_path_smoke` 映射到 `BIGOS_TCP_PATH_SMOKE` 宏，遵循既有 smoke 宏映射模式。
- [x] 8.3 在 `kernel/core/net`（`tcp.cc` 或新增 TCP smoke 单元）实现 `#ifdef BIGOS_TCP_PATH_SMOKE` 内核内部闭环入口：以 `LoopbackReady`（仅本机配置、无帧级设备）初始化 context；驱动本机地址 TCP 三次握手（主动+被动）；`Established` 有序双向数据交付（断言按序读到相同数据、`tcp_segments_rx`/`tcp_segments_tx`/`tcp_connections_opened` 确定性递增）；正常 FIN 双向拆除与标准 `2*MSL` `TIME_WAIT` 回收（断言 `tcp_connections_closed` 递增、TCB 槽回收可复用；smoke 可用较短 `TCP_MSL_TICKS` 配置或直接驱动 `tcp_pump` 到期以在有界时间内验证）；发 COM1 `BIGOS_TCP_PATH_PASSED`/`BIGOS_TCP_PATH_FAILED` 标记。
- [x] 8.4 在同一 smoke 入口新增错误与边界路径覆盖：注入乱序段（断言有界重组后按序交付）、重复/超窗段（断言 `tcp_dropped` 递增且不破坏有序数据）、触发 RFC 6298 重传路径（构造丢段或直接驱动 `tcp_pump` 到期，断言 `tcp_retransmits` 递增、`rto` 落在 `[TCP_RTO_MIN, TCP_RTO_MAX]` 且按指数退避加倍、Karn 算法对重传段不采样 RTT）、重传超限复位（断言 `tcp_resets` 递增并回收 TCB）、非法握手段、填满连接表（断言 `TableFull`/`tcp_dropped`）、`TIME_WAIT` 槽占满时表满与被动/同时关闭。将断言结果并入同一 `BIGOS_TCP_PATH_PASSED`/`_FAILED` 标记。
- [x] 8.5 在 `kernel/core/kernel.cc` 的 smoke 阶段按既有模式以 `#ifdef BIGOS_TCP_PATH_SMOKE` 守卫 spawn 该入口线程（含相应头包含）；确认默认（开关关闭）构建不含该线程、默认启动行为不变。

## 9. 构建与静态检查

- [x] 9.1 运行 `xmake`（x86_64-elf-gcc 交叉构建）确认默认配置（`--tcp_path_smoke=n`）编译通过且默认启动行为不变；若交叉工具链不可用则显式记录该阻塞与残留风险。
- [x] 9.2 运行 `xmake f --tcp_path_smoke=y && xmake` 确认 smoke 配置编译通过；随后 `xmake f --tcp_path_smoke=n` 复位默认配置。
- [x] 9.3 对新增/修改的 C++ 文件（`include/bigos/net.h`、`include/bigos/net/tcp.h`、`kernel/core/net/tcp.cc`、`kernel/core/net/protocol.cc` 及 smoke 单元）执行 clang 辅助静态检查，配置尽量贴近 GCC 交叉环境（freestanding C++17、`--target=x86_64-elf`、项目 include 路径、no hosted runtime、`-fno-exceptions`、`-fno-rtti`、`-Wall -Wextra`）；修复本次变更引入的 clang 错误与有效新增告警。若等价 clang 标志不可用则记录差距与残留风险。
- [x] 9.4 对上述文件执行 clangd 辅助诊断；区分历史诊断、本次变更诊断与 freestanding/工具链误报，修复本次引入的问题。
- [x] 9.5 TCP 专项复核：段大小不超过既有 IPv4 载荷缓冲上界（`static_assert` 成立）；序号回绕安全比较正确；RFC 6298 估计器全整数/移位无浮点、`rto` clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`、Karn 算法生效；标准 `2*MSL` `TIME_WAIT` 到期回收且槽占满时确定性 `TableFull`；连接表/发送/接收缓冲/重传队列/重组槽均编译期定容且满时确定性处理；本机闭环校验和归一化自洽；TCP 分支不影响既有 UDP/ICMP/ARP/loopback 路径、对外 TX/RX ownership 与 IRQ 边界零回归。

## 10. 源级契约与仿真器 smoke

- [x] 10.1 新增/扩展源级契约测试（`tests/`）：断言 `IPV4_PROTOCOL_TCP == 6`、TCP 容量常量与 `TCP_MSS` 上界关系、`Diagnostics` append-only（新增 TCP 计数位于既有 `loopback_dropped` 之后、`last_status` 之前，既有字段偏移不变）、`handle_ipv4` 追加 TCP 分支、TCP 段经 `send_ipv4` 输出、无新增 syscall/socket ABI、smoke 默认关闭。运行 `uv run pytest <目标用例>`；`uv` 或用例不适用时显式记录。
- [x] 10.2 通过 QEMU headless 路径运行 smoke（启用 `tcp_path_smoke`）并期待 COM1 `BIGOS_TCP_PATH_PASSED`，例如 `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/tcp_smoke_serial.log --expect-serial-marker BIGOS_TCP_PATH_PASSED`；若 QEMU/工具链/镜像不可用则显式记录跳过原因与残留风险。
- [x] 10.3 运行一次默认配置的 QEMU headless 默认启动回归（期待既有 `BIGOS_USER_EXEC` marker），确认 TCP 改动未影响默认启动进入 shell；不可用时记录跳过与残留风险。

## 11. 文档与验证记录

- [x] 11.1 在 `docs/en` 更新网络路径说明，新增内核内部有界 TCP 语义（IPv4 协议号 6 分发、经既有 IPv4 输出层承载/loopback 闭环、定容 TCB 与状态机、RFC 6298 动态 RTT 估计与重传/窗口、有序交付/重组、连接拆除与标准 `2*MSL` `TIME_WAIT`、TCP 诊断计数、默认关闭 smoke 与 marker、非目标边界——不实现完整 TCP 特性矩阵、不暴露 stream socket 用户接口），并在 `docs/zh` 同步对应相对路径镜像；使用仓库相对路径，不暗示 boot/链接/向量/页表/磁盘/既有 ABI 编号变更。
- [x] 11.2 在 change 目录整理验证记录（`validation.md`），分别列出改动范围、已通过的检查（默认/smoke 构建、QEMU 双 marker、clang、pytest 基线 diff）、TCP 专项复核、因环境无法运行的检查（含原因与残留风险）、历史诊断与本次变更引入并已解决的问题。
