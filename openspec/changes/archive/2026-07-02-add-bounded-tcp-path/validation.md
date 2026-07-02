# add-bounded-tcp-path 验证记录

本变更在内核内部有界网络协议路径（`bigos::net`）之上新增一条内核内部有界 TCP
协议路径：IPv4 输入分发识别协议号 6（TCP）交给 `handle_tcp`，TCP 段经既有
`send_ipv4` 输出层发送（本机地址复用 loopback 分流、对外复用 `route_destination`
+ ARP + 帧级设备），实现定容 TCB 池与 TCP 状态机、三次握手、RFC 6298 动态 RTT
估计与有界重传/窗口、序号驱动的有序交付与有界重组、正常/被动/同时关闭与标准
`2*MSL` `TIME_WAIT` 回收、RST/重传超限/非法段确定性复位。不新增或改动任何
syscall 编号、`int 0x80` ABI 或 socket ABI，不暴露 `connect`/`listen`/`accept`
用户接口，不改动默认启动、boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。

## 改动范围

网络协议头（`include/bigos/net.h`）：

- 在 `IPV4_PROTOCOL_ICMP`/`IPV4_PROTOCOL_UDP` 附近追加 `IPV4_PROTOCOL_TCP = 6`
  （既有常量取值不变）。
- 追加 TCP 有界容量与计时常量：`TCP_CONNECTION_CAPACITY`、`TCP_MSS`、
  `TCP_SEND_BUFFER`/`TCP_RECV_BUFFER`、`TCP_RETX_QUEUE_CAPACITY`、
  `TCP_REORDER_SLOTS`、`TCP_RTO_MIN`/`TCP_RTO_MAX`、`TCP_MSL_TICKS`、
  `TCP_MAX_RETRANSMIT`；以 `static_assert` 守护 `20+20+TCP_MSS <=
  20+UDP_MAX_PAYLOAD+8` 与 `<= DEFAULT_MTU`。
- `Diagnostics` 末尾（`loopback_dropped` 之后、`last_status` 之前）append-only
  追加 `tcp_segments_rx`/`tcp_segments_tx`/`tcp_retransmits`/
  `tcp_connections_opened`/`tcp_connections_closed`/`tcp_resets`/`tcp_dropped`，
  新增 3 条 `__builtin_offsetof` `static_assert` 守护 append 顺序（既有 loopback
  守护保留）。
- 新增内核内部 IPv4 输出转发声明 `ipv4_send`（复用既有 `send_ipv4`）。

TCP 状态机头（`include/bigos/net/tcp.h`，新建）：

- `enum class TcpState`（Closed/Listen/SynSent/SynReceived/Established/FinWait1/
  FinWait2/Closing/CloseWait/LastAck/TimeWait）。
- `struct TcpControlBlock`：四元组、状态、发送侧 `snd_una`/`snd_nxt`/`snd_wnd` +
  定容发送缓冲 + 定容重传队列（含 `send_tick`/`rto_deadline_tick`/
  `retransmit_count`/`retransmitted`）、RFC 6298 `srtt`/`rttvar`/`rto`（tick 整数）、
  接收侧 `rcv_nxt`/`rcv_wnd` + 定容接收缓冲 + 定容乱序重组槽、
  `time_wait_deadline_tick`、`active`。
- 内核内部 API：`tcp_open`/`tcp_listen`/`tcp_send`/`tcp_receive`/`tcp_close`/
  `handle_tcp`/`tcp_pump`/`tcp_reset_state`，`BIGOS_TCP_PATH_SMOKE` 下声明
  `tcp_path_smoke_entry`。

TCP 状态机实现（`kernel/core/net/tcp.cc`，新建）：

- 定容 TCB 池、四元组精确匹配、`LISTEN` 按本地端口匹配、分配（满则 `TableFull`
  + `tcp_dropped`）与回收（清零复用）；32 位回绕安全序号比较（有符号差值，注释
  记录该非显然约束）。
- 主动/被动三次握手；累积 ACK 推进 `snd_una` 并回收重传队列；按序接收 + 有界乱序
  重组（缺口填补后合并）；重复/超窗/校验失败确定性丢弃。
- RFC 6298 估计器（整数/移位，alpha=1/8、beta=1/4，`rto = srtt + max(G, 4*rttvar)`
  clamp 到 `[TCP_RTO_MIN, TCP_RTO_MAX]`，Karn 算法排除重传段）；`tcp_pump` 超时重传
  指数退避（`rto = min(rto*2, TCP_RTO_MAX)`，不改写 srtt/rttvar），超限复位并回收。
- 通告窗口以接收缓冲剩余空间流控；正常/被动/同时关闭；标准 `2*MSL` `TIME_WAIT`
  于 `tcp_pump` 到期回收；匹配 RST/超限/非法段复位。
- TCP 校验和（IPv4 伪首部 + TCP 头 + 数据），本机闭环把伪首部源/宿归一化为
  `local_ipv4`，与 `send_ipv4` IPv4 头一致，`handle_tcp` 重建自洽。
- `BIGOS_TCP_PATH_SMOKE` 下的 LoopbackReady 闭环 smoke（见下）。

协议层（`kernel/core/net/protocol.cc`）：

- `handle_ipv4` 协议号分发追加 `IPV4_PROTOCOL_TCP` 分支：`tcp_segments_rx++` 后调用
  `handle_tcp(ctx, source, dest, body, body_len)`，既有 ICMP/UDP 分发、
  `ipv4_unsupported_protocol` 语义与本机过滤不变。
- 新增 `ipv4_send`（loopback-capable 就绪门 + `send_ipv4`），供 TCP 复用单一 IPv4
  输出层，不另建第二套输出/校验逻辑；含 `bigos/net/tcp.h` 头包含。

构建与入口：

- `xmake/options.lua`：新增默认关闭 `tcp_path_smoke` 选项。
- `xmake/kernel.lua`：映射到 `BIGOS_TCP_PATH_SMOKE` 宏。
- `kernel/core/kernel.cc`：`#ifdef BIGOS_TCP_PATH_SMOKE` 守卫 spawn
  `tcp_path_smoke_entry`（同 `#ifdef` 下包含 `bigos/net/tcp.h`）。

测试与文档：

- `tests/test_bounded_tcp_path_source.py`（新建）：TCP 协议号取值与位置、容量/MSS
  上界关系、`Diagnostics` append-only + offset 守护、TcpState/TCB/内部 API、
  `send_ipv4` 复用 + RFC 6298 整数估计器、`handle_ipv4` TCP 分支、默认关闭 smoke、
  无新 syscall/socket ABI 契约。
- `docs/en/arch/bounded-network-protocol-path.md` +
  `docs/zh/arch/bounded-network-protocol-path.md`：同相对路径双语新增
  “Bounded TCP Path / 有界 TCP 路径” 一节，并修订既有“不暴露 TCP”非目标行。

## 已通过的检查

- `xmake`（x86_64-elf-gcc 默认交叉构建，`--tcp_path_smoke=n`）：编译通过
  （build ok），默认启动路径不变。
- `xmake f --tcp_path_smoke=y && xmake`：smoke 配置编译通过（build ok）；随后
  `xmake f --tcp_path_smoke=n` 复位默认。
- QEMU headless smoke（UEFI，smoke 开）：
  `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/tcp_smoke_serial.log --expect-serial-marker BIGOS_TCP_PATH_PASSED`
  → 观察到 `BIGOS_TCP_PATH_PASSED`，无 `_FAILED`。覆盖：本机地址三次握手（主动+被动）、
  `Established` 有序双向数据交付（`tcp_segments_rx`/`tcp_segments_tx`/
  `tcp_connections_opened` 递增）、有界乱序重组按序合并、重复段确定性丢弃
  （`tcp_dropped` 递增）、正常主动+被动关闭（`tcp_connections_closed` 递增）与标准
  `2*MSL` `TIME_WAIT` 回收（TCB 槽全部复用）、RFC 6298 重传（`tcp_retransmits`
  递增、`rto` 翻倍且落在 `[TCP_RTO_MIN, TCP_RTO_MAX]`、Karn 标志置位）、重传超限
  复位（`tcp_resets` 递增并回收）、连接表满（`TableFull` + `tcp_dropped`）、非法段
  （`NotBound` + `tcp_dropped`）。
- QEMU headless 默认启动回归（smoke 关）：`--expect-serial-marker BIGOS_USER_EXEC`
  → 观察到 `BIGOS_USER_EXEC`（日志 `logs/tcp_default_boot.log`），默认进入 userland
  行为未变。
- clang 辅助静态检查（freestanding C++17、`--target=x86_64-elf`、no hosted runtime、
  `-fno-exceptions`、`-fno-rtti`、`-Wall -Wextra`、项目 include 路径含
  `cpp/include`/`cpp/libsupc++/include`）：`kernel/core/net/tcp.cc` 与
  `kernel/core/net/protocol.cc`（均带 `-DBIGOS_TCP_PATH_SMOKE -DBIGOS_USER_PROCESS`）
  0 error、0 告警；`include/bigos/net/tcp.h` 单独语法检查 0 告警。修复了本次引入的
  两处 clang 新增告警（未使用的 `seq_geq`/`emit_rst`，已删除）。
- Python 校验：`uv run pytest tests/test_bounded_tcp_path_source.py` → 7 passed；
  `uv run pytest tests/test_loopback_network_path_source.py` → 与本次 net.h 布局
  变更兼容，通过；`uv run pytest tests/test_bilingual_docs_layout.py` → 4 passed。
- 全量回归基线核对：`uv run pytest` 全量为 `20 failed, 340 passed`；`git stash`
  移除本次改动后复跑为 `20 failed, 333 passed`，失败用例集合逐行 `diff` 完全一致
  （IDENTICAL，本次新增失败 0，新增通过 7）。这 20 项既有失败均属其他子系统的源级
  契约漂移（vmem/CR3/TLB flush、fork/COW、scheduler IRQ 分配、user ELF loader、
  userland smoke、metadata、storage role、source-root layout），与本变更文件
  （`net.h`/`net/tcp.h`/`tcp.cc`/`protocol.cc`/`kernel.cc`/`xmake`/docs）无关。

## TCP 专项复核

- 段大小上界：`static_assert(20+20+TCP_MSS <= 20+UDP_MAX_PAYLOAD+8)` 与
  `<= DEFAULT_MTU` 成立（`TCP_MSS = 500`），TCP 段作为 IPv4 载荷不越过既有
  `send_ipv4` 栈缓冲与 MTU，不做 IP 分片。
- 序号回绕安全：`seq_lt/leq/gt` 用 `(int32_t)(a-b)` 有符号差值，跨 2^32 回绕点
  比较正确；smoke 的握手/数据/重组序号推进覆盖典型路径。
- RFC 6298 全整数无浮点：估计器仅用移位（`>>3`/`>>2`）与加减，`rto` clamp 到
  `[TCP_RTO_MIN, TCP_RTO_MAX]`，`max(G, 4*rttvar)` 保证下界非 0；Karn 算法对
  `retransmitted` 段不采样（`process_ack` 内 `if (!e.retransmitted)`）。契约测试
  以正则确认无 `float`/`double` 声明类型。
- 标准 `2*MSL` `TIME_WAIT`：`time_wait_deadline_tick = ticks() + 2*TCP_MSL_TICKS`，
  `tcp_pump` 到期回收使槽可复用；`TIME_WAIT`/其他槽占满时新建连接确定性
  `TableFull`，不提前回收。
- 编译期定容且满时确定性：连接表满 `TableFull`、重传队列满 `QueueFull`、乱序重组槽
  满 `tcp_dropped`、接收缓冲满通告有界（含 0）窗口；均不无界缓冲。
- 本机闭环校验和归一化自洽：`transmit_segment` 与 `handle_tcp` 均以 `local_ipv4`
  作为本机目的伪首部宿地址，`send_ipv4` IPv4 头同步归一化，本机 send→receive
  校验和通过，smoke 全程不触 `Malformed`。
- 对既有路径与边界零回归：`handle_ipv4` 仅追加 TCP 分支，ICMP/UDP/ARP/loopback、
  对外 TX/RX ownership、`ordinary_context()` 边界未改；`tcp_pump`/`handle_tcp`/
  发送/RTT 采样均在 ordinary 上下文（`tcp_pump` 非 ordinary 时
  `irq_context_rejected++` + `UnsupportedContext`），绝不从 IRQ 分配/操作 TCB。
  默认 `g_default_context` 零初始化即 `Disabled`，normal boot 不初始化 TCP。

## 因环境无法独立运行 / 残留风险

- clangd LSP 会话（任务 9.4 范围内的辅助诊断）：本环境未单独运行 clangd `--check`
  交互会话；以 clangd 同源的 clang 前端 `-fsyntax-only`（相同 freestanding/target
  flags）对全部新增/修改文件做等价诊断，0 error、0 有效新增告警。`include/bigos/net.h`
  单独编译时报既有共享常量 `-Wunused-const-variable`（ETHERNET_* 等），属头文件
  独立编译噪声、非本次引入，随 .cc 传递包含时不出现。权威检查仍是 GCC 交叉构建。
- 对端/丢包为模拟：smoke 在单一 LoopbackReady context 内以“回收对端 TCB / 直接注入
  原始段 / 直接把 `rto_deadline_tick` 置为当前 tick”方式构造乱序、重复、丢段与
  重传到期，未经真实网络乱序/丢包/延迟。残留风险：真实网卡/tap 下的乱序、丢包、
  跨主机 RTT 与 MSS 协商未在本变更内回归，留待 stream socket 用户接口能力引入时
  一并端到端验证。
- 无用户接口：本变更不暴露 `connect`/`listen`/`accept` 或任何新 syscall/fd/socket
  ABI；用户态无法直接驱动 TCP。跨进程用户态 TCP 端到端回归不在本变更范围。
- 全量 pytest 既有 20 项失败：属其他子系统源级契约漂移，非本变更引入（已用
  `git stash` 前后 diff 证明集合一致），本变更不负责修复。

## 本次变更引入并已解决的问题

- smoke 首跑 `BIGOS_TCP_PATH_FAILED timewait-recycle`：原顺序先对真实连接注入原始
  乱序/重复段（故意打乱序号空间），再对同一对 TCB 做正常关闭，导致关闭握手的 ACK
  序号不匹配、连接卡在 `FinWait1` 无法进入 `TIME_WAIT` 回收。已改为先在纯净连接上
  完成握手→双向数据→主动+被动关闭→`2*MSL` 回收，再用独立的一次性连接做乱序/重复
  注入（用后 `tcp_reset_state` 丢弃），QEMU headless 复测得 `BIGOS_TCP_PATH_PASSED`。
- 同步 loopback 下关闭计数二义：`tcp_close` 发 FIN 后对端 ACK 在同一调用栈内同步
  回递，若在 `emit_new` 之后再改状态/计数会读到已迁移或已回收的 TCB。已把状态迁移与
  `tcp_connections_closed++` 前置到 `emit_new` 之前，计数改为每次本地发起关闭恰好
  +1（不在被动收 FIN 或 `LastAck` 完成处重复计数）。
- `memcmp` 未声明：内核 freestanding 运行时不提供 `memcmp`，smoke 数据比对报错。
  已在 smoke 匿名命名空间新增 `bytes_equal` 逐字节比较替代。
- clang 新增 `-Wunused-function`（`seq_geq`/`emit_rst`）：GCC -O2 静默丢弃、clang
  报告。已删除这两个未使用的辅助函数，GCC 双配置与 clang 均 0 告警。
- 误删既有 loopback offset 守护：追加 TCP 守护时替换掉了
  `last_status > loopback_dropped` 断言，导致既有
  `test_loopback_network_path_source.py` 失败。已补回该守护（仍成立）与新增 TCP
  守护并存，两套契约测试均通过。
