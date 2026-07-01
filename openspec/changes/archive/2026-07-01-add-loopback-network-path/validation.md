# add-loopback-network-path 验证记录

本变更在内核内部有界网络协议路径（`bigos::net`）之上新增一条本机地址 loopback
路径：IPv4 输出层识别本机地址目的（配置 `local_ipv4` 或整个回环网段
`127.0.0.0/8`），把发往本机地址的 IPv4 报文（承载 ICMP/UDP）直接闭环回本机
IPv4 输入分发，无需 ARP、无需帧级 `NetworkDevice`；并在无 ready 设备但有有效
本机 IPv4 配置时进入 loopback-only 就绪模式，对外（非本机）发送仍要求 ready 设备。
不新增或改动任何 syscall 编号、`int 0x80` ABI 或 UDP socket ABI，不改动默认启动、
boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。

## 改动范围

网络协议头（`include/bigos/net.h`）：

- 末尾追加回环常量 `IPV4_LOOPBACK = 0x7f000001u`、`IPV4_LOOPBACK_PREFIX =
  0x7f000000u`、`IPV4_LOOPBACK_MASK = 0xff000000u`（既有常量取值不变）。
- `enum class State` 末尾追加 `LoopbackReady`（既有枚举值不变）。
- `Diagnostics` 末尾追加 `loopback_delivered`/`loopback_dropped`（置于
  `udp_payload_overflow` 之后、`last_status` 之前），并新增 5 条
  `__builtin_offsetof` `static_assert` 守护既有布局与 append 顺序。
- 在 `BIGOS_LOOPBACK_NETWORK_SMOKE` 下声明 `loopback_network_smoke_entry`。

协议层（`kernel/core/net/protocol.cc`）：

- 新增文件内只读谓词 `is_local_delivery(ctx, dest)`（`dest == local_ipv4` 或
  `(dest & IPV4_LOOPBACK_MASK) == IPV4_LOOPBACK_PREFIX`），无副作用。
- `send_ipv4`：先分类目的、构造完整 IPv4 报文（本机目的把宿地址归一化为
  `local_ipv4`）；本机目的直接交 `handle_ipv4` 并按结果递增
  `loopback_delivered`/`loopback_dropped`，不经 ARP/transmit；非本机目的显式要求
  `state == Ready && device != nullptr`（否则 `NotReady`），再走既有
  `route_destination` + `arp_resolve` + `transmit_ethernet`。为前向调用新增
  `handle_ipv4` 声明。
- `handle_ipv4`：本机过滤由 `dest == local_ipv4 || BROADCAST` 放宽为
  `is_local_delivery(ctx,dest) || BROADCAST`；非本机仍 `ipv4_not_local` 丢弃；
  头部/长度/分片/校验和/分发逻辑零改动。
- `handle_icmp`：合法 echo reply（type 0、code 0）作为终止性 accept 直接返回
  `Ok`，不再生成新 request（回环 echo-to-self 递归深度 ≤1）。
- 新增 `validate_local_ready`（接受 `Ready`+device 或 `LoopbackReady`，复用
  `ordinary_context()`→`UnsupportedContext`）；`udp_bind`/`udp_send_to` 改用它，
  `pump`/`inject_frame`/`arp_resolve` 保留设备严格 `validate_ready`。
- `udp_send_to`：把本机目的的伪首部校验和宿地址归一化为 `local_ipv4`，与
  `handle_udp` 重建端一致。
- `init`：无 ready 设备但 `local_ipv4` 非零时进入 `State::LoopbackReady`
  （device=nullptr），无有效配置仍 `SkippedNoDevice`。
- 在 `BIGOS_LOOPBACK_NETWORK_SMOKE` 下新增 `loopback_network_smoke_entry`：
  LoopbackReady 初始化 → bind → UDP 本机闭环（断言 payload/来源地址/来源端口与
  `loopback_delivered` 递增）→ no-data → 未绑定端口（`NotBound` + `loopback_dropped`
  递增）→ 队列满（发满 `UDP_RX_QUEUE_CAPACITY` 再发得 `QueueFull` + `loopback_dropped`
  递增）→ 非本机目的（无设备 `NotReady`）→ ICMP echo-to-self（断言
  `icmp_echo_requests`/`icmp_echo_replies` 各 +1、`loopback_delivered` ≥+2）→ COM1
  `BIGOS_LOOPBACK_NETWORK_PASSED`/`_FAILED`。

构建与入口：

- `xmake/options.lua`：新增默认关闭 `loopback_network_smoke` 选项。
- `xmake/kernel.lua`：映射到 `BIGOS_LOOPBACK_NETWORK_SMOKE` 宏。
- `kernel/core/kernel.cc`：`#ifdef BIGOS_LOOPBACK_NETWORK_SMOKE` 守卫 spawn 入口
  线程（`net.h` 已无条件包含）。

测试与文档：

- `tests/test_loopback_network_path_source.py`（新建）：常量取值、`Diagnostics`
  append-only + offset 守护、`State::LoopbackReady` 末尾、分流/归一化/就绪门/
  ICMP 终止/默认关闭 smoke/无新 syscall-socket ABI 契约。
- `docs/en/arch/bounded-network-protocol-path.md` +
  `docs/zh/arch/bounded-network-protocol-path.md`：同相对路径双语新增
  “Local-Address Loopback Path / 本机地址 Loopback 路径” 一节。

## 已通过的检查

- `xmake`（x86_64-elf-gcc 12.2.0 默认交叉构建，`--loopback_network_smoke=n`）：
  编译通过（build ok），默认启动路径不变。
- `xmake f --loopback_network_smoke=y && xmake`：smoke 配置编译通过（build ok）；
  随后 `xmake f --loopback_network_smoke=n` 复位默认。
- QEMU headless smoke（QEMU 11.0.1，UEFI，smoke 开）：
  `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/loopback_smoke_serial.log --expect-serial-marker BIGOS_LOOPBACK_NETWORK_PASSED`
  → 观察到 `BIGOS_LOOPBACK_NETWORK_PASSED`，无 `_FAILED`。覆盖：LoopbackReady 初始化、
  UDP 本机闭环 payload/来源/`loopback_delivered`、no-data、未绑定+队列满
  `loopback_dropped`、非本机 `NotReady`、ICMP echo-to-self 计数与有界深度。
- QEMU headless 默认启动回归（smoke 关）：`--expect-serial-marker BIGOS_USER_EXEC`
  → 观察到 `BIGOS_USER_EXEC`（日志 `logs/loopback_default_boot_serial.log`），默认
  进入 userland 行为未变。
- clang 辅助静态检查（freestanding C++17、`--target=x86_64-elf`、no hosted runtime、
  `-fno-exceptions`、`-fno-rtti`、`-Wall -Wextra`、项目 include 路径含
  `cpp/include`/`cpp/libsupc++/include`）：`kernel/core/net/protocol.cc`（带
  `-DBIGOS_LOOPBACK_NETWORK_SMOKE -DBIGOS_USER_PROCESS`）与
  `kernel/core/net/socket.cc` 均 0 error、0 告警；`include/bigos/net.h` 经二者
  传递包含覆盖。
- Python 校验：`uv run pytest tests/test_loopback_network_path_source.py
  tests/test_virtio_net_driver_source.py` → 10 passed；
  `uv run pytest tests/test_bilingual_docs_layout.py` → 4 passed。
- 全量回归基线核对：`uv run pytest` 全量为 `20 failed, 333 passed`；`git stash`
  移除本次改动后复跑为 `20 failed`，失败用例集合逐行 `diff` 完全一致
  （IDENTICAL，本次新增失败 0）。这 20 项既有失败均属其他子系统的源级契约漂移
  （`SYS_POLL`/`ftruncate`/vmem TLB flush/CR3 激活/userland smoke/ELF loader），
  与本变更文件（`net.h`/`protocol.cc`/`kernel.cc`/`xmake`/docs）无关。

## 网络协议路径专项复核

- 本机过滤放宽不越权：`handle_ipv4` 仅接受本机地址集合与既有广播；非本机一律
  `ipv4_not_local` 丢弃计数，对外 RX 帧的过滤等价于原 `local_ipv4` 语义（本机
  目的经 `send_ipv4` 归一化为 `local_ipv4`）。
- 源/宿地址域自洽：`udp_send_to`（伪首部）与 `send_ipv4`（IPv4 头）双侧把本机目的
  归一化为 `local_ipv4`，与 `handle_udp` 重建端（源=报文源、宿=`local_ipv4`）一致，
  UDP 本机 send→receive 校验和通过，无特例校验。
- 有界递归深度（≤1）：`send_ipv4`→`handle_ipv4`→`handle_udp` 入队即止；ICMP echo
  request 生成一次 reply，reply 再进 `handle_icmp` 被识别为 reply 终止，不产生新
  request。
- loopback 就绪不误启用协议栈：`LoopbackReady` 仅放行 `udp_bind`/`udp_send_to`
  的本机投递；`pump`/`inject_frame`/`arp_resolve` 仍设备严格；`SYS_SOCKET` 仍要求
  `State::Ready`。默认 `g_default_context` 零初始化即 `Disabled`，normal boot 不调用
  `init_default`。
- 容量/未绑定复用既有确定性状态：`handle_udp` 的 `QueueFull`/`NotBound` 语义未改；
  loopback 仅追加 `loopback_delivered`/`loopback_dropped` 标注命中来源。
- 对外 RX/TX ownership 与 IRQ 边界零回归：`transmit_ethernet`/`pump`/RX 归还路径、
  `ordinary_context()` 边界与 `wake_all` 调用点未改；loopback 分流不新增 IRQ 入口。

## 因环境无法独立运行 / 残留风险

- clangd（任务 5.4 范围内的辅助诊断）：`clangd --check` 报 “4 errors”，经核实全部为
  `tweak: ExtractFunction ==> FAIL`（refactor 可用性探测，非编译诊断，由 smoke
  循环内 break/continue 触发）；以 clangd 相同 cc1 flags 运行 `clang -cc1` 产生 0 个
  `error:`；同目录未改动的 `socket.cc` clangd 0 errors，无关的 `kmem.cc` 116 errors
  属既有 freestanding 误报基线。权威检查仍是 GCC 交叉构建，未单独跑 clangd LSP 会话。
- 用户态端到端 loopback：本变更不新增用户接口，用户程序经既有 UDP socket 面向
  `127.0.0.1`/本机地址即受益；但跨进程用户态 `sendto`/`recvfrom` 面向本机地址的
  端到端回归未在本变更内单独构造，需后续用户态 smoke 或 TCP 路径引入时一并回归。
  残留风险：用户态 socket 层对 `LoopbackReady`-only context 的 `SYS_SOCKET` 目前
  仍要求 `State::Ready`（默认 context 需 ready 设备），本变更未放宽该用户态门槛，
  仅提供内核内部 loopback 基座。
- 全量 pytest 既有 20 项失败：属其他子系统源级契约漂移，非本变更引入（已用
  `git stash` 前后 diff 证明集合一致），本变更不负责修复。

## 本次变更引入并已解决的问题

- 编辑 `send_ipv4` 前置声明时误删 `route_destination` 函数体，导致语法破坏。已在同
  一处补回 `route_destination` 完整实现并新增 `handle_ipv4` 前向声明，默认与 smoke
  两种配置均编译通过。
