# add-loopback-network-path 任务清单

按子系统拆分，依赖顺序自上而下：先在网络协议头补齐本机地址判定与回环常量，再在协议层落地 loopback 分流与初始化放宽，随后补 smoke、构建/静态检查、源级契约与仿真器验证、文档与验证记录。改动集中在 `include/bigos/net.h` 与 `kernel/core/net/protocol.cc`（C++），涉及 `xmake/` 构建开关与 `kernel/core/kernel.cc` smoke 入口。按规则附 clang/clangd 辅助静态检查与 QEMU headless smoke 验证。新增枚举/字段/常量一律末尾追加，不重排既有布局，并以 `static_assert` 守护关键既有结构（如 `Context`/`Diagnostics`/`UdpEndpoint`）不受影响。不新增或改动任何 syscall/socket ABI，不改动 boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。

## 1. 网络协议头：本机地址判定与回环常量

- [x] 1.1 在 `include/bigos/net.h` 增补回环地址常量（如 `constexpr uint32_t IPV4_LOOPBACK = 0x7f000001u`，即 `127.0.0.1`）与回环网段判定所需的掩码/前缀常量（`127.0.0.0/8`，即前缀 `0x7f000000u`、掩码 `0xff000000u`），置于既有常量末尾，不改动既有常量取值。
- [x] 1.2 声明只读内部判定的公开边界：若判定需跨编译单元复用，则在 `bigos::net` 增声明 `bool is_local_address(const Context *__ctx, Ipv4Address __dest) noexcept`（本机 `local_ipv4` 或回环网段 `127.0.0.0/8` 返回 true）；否则在 `protocol.cc` 内以文件内静态谓词实现并在本任务记录该决定。回环判定 MUST 覆盖整个 `127.0.0.0/8`（判定 `(dest & 0xff000000) == 0x7f000000`），非仅精确 `127.0.0.1`。判定 MUST 只读、无副作用。
  - 决定：判定无跨编译单元复用需求（smoke 复用 `udp_send_to`/`udp_receive_from`，不直接调用谓词），故以 `protocol.cc` 内文件静态谓词 `is_local_delivery(ctx, dest)` 实现，不在 `net.h` 增公开声明；判定只读、无副作用，覆盖 `local_ipv4` 与整个 `127.0.0.0/8`（`(dest & IPV4_LOOPBACK_MASK) == IPV4_LOOPBACK_PREFIX`）。
- [x] 1.3 在 `Diagnostics` 末尾追加 `loopback_delivered`（本机地址报文成功交付本机输入路径的次数）与 `loopback_dropped`（本机地址报文经 loopback 路径因未绑定/队列满/校验失败被丢弃的次数）两个计数字段，以支撑确定性验证区分 loopback 与对外路径命中。新增字段一律末尾追加，并以 `static_assert` 守护既有 `Diagnostics` 字段偏移不变；不改动既有计数（`udp_received`/`ipv4_rx`/`icmp_echo_*` 等）取值语义。

## 2. 协议层 loopback 分流

- [x] 2.1 在 `kernel/core/net/protocol.cc` 的 `send_ipv4` 中，在构造完整 IPv4 报文后、进入 `route_destination`/`arp_resolve` 之前插入本机地址分流：目的为本机地址集合（`local_ipv4` 或 `127.0.0.0/8`）时不调用 `transmit_ethernet`，而是把刚构造的 IPv4 报文交给 `handle_ipv4(__ctx, packet, total_len)`；非本机目的保持既有 `route_destination` + `arp_resolve` + `transmit_ethernet` 路径与语义不变。在 loopback 分流点按结果递增 `loopback_delivered`（成功交付）/`loopback_dropped`（未绑定/队列满/校验失败），不改动既有计数语义。
- [x] 2.2 统一本机闭环源/宿地址域：本机地址闭环以 `local_ipv4` 作为源/宿域，`127.0.0.1` 归一化为 `local_ipv4` 进入输入路径，使 `handle_udp` 重建伪首部校验和（源=报文源、宿=`local_ipv4`）自洽，无需为 loopback 增设特例校验。以 UDP 本机 send→receive 校验和通过为准绳验证归一化正确。
- [x] 2.3 放宽 `handle_ipv4` 的本机过滤：当前 `dest == local_ipv4 || dest == BROADCAST` 扩展为接受本机地址集合（`local_ipv4`、回环网段）与既有广播；非本机目的仍按 `ipv4_not_local` 丢弃并计数。确认 ICMP/UDP 分发、头部/长度/分片/校验和校验逻辑零改动。
- [x] 2.4 复核有界递归深度：`send_ipv4`→`handle_ipv4`→`handle_udp` 入队即止；`handle_icmp` 仅对 echo request 生成一次 echo reply，reply 经 `send_ipv4` 本机分流再进 `handle_ipv4` 时被识别为 echo reply（非 request）不再生成新 reply，递归深度有界（≤1）。在代码注释仅记录该非显然的有界深度约束（WHY），不赘述流程。
- [x] 2.5 复核上下文边界：loopback 分流复用 `handle_ipv4`/`handle_udp`/`wake_all`，必须在 ordinary（可阻塞、非 IRQ）上下文执行。确认 `udp_send_to`/`send_ipv4` 既有 `ordinary_context()`/`UnsupportedContext` 边界覆盖 loopback 分流，MUST NOT 从 IRQ 触发本机投递或唤醒。
  - 复核：`udp_send_to`/`udp_bind` 经 `validate_local_ready` 拒绝 IRQ/nonblocking（复用 `ordinary_context()`→`UnsupportedContext`）；`send_ipv4` 仅由 `udp_send_to`/`handle_icmp` 调用，两者均在已通过 ordinary-context 校验的路径内，loopback 分支不新增 IRQ 入口。

## 3. 初始化与禁用语义放宽

- [x] 3.1 在 `kernel/core/net/protocol.cc` 的 `init()`/就绪判定中引入独立于帧级设备的“loopback 就绪”：有有效本机 IPv4 配置但无 ready `NetworkDevice` 时，MUST 保持本机地址 loopback 路径可用；对外发送分支仍要求 `device` 存在且 ready，缺失时返回既有 `NotReady`/设备状态。避免让本无网络配置的默认启动意外进入 Ready。
  - 实现：新增 `State::LoopbackReady`（末尾追加，不改既有枚举值）；`init` 在无 ready 设备但 `local_ipv4` 非零时进入 `LoopbackReady`，无配置时保持既有 `SkippedNoDevice`；`udp_bind`/`udp_send_to` 改用 `validate_local_ready`（接受 `Ready`+设备 或 `LoopbackReady`）；`send_ipv4` 对外分支显式要求 `state == Ready && device != nullptr`，否则返回 `NotReady`。
- [x] 3.2 复核对外路径 not-ready 行为：无 ready 设备时对非本机目的发送返回确定性 not-ready/设备状态且不误报成功；确认 `validate_ready`/`State` 语义在 loopback 就绪模式下对“对外发送”与“本机投递”给出各自确定性结果。
  - 复核：`LoopbackReady` 下对非本机目的 `send_ipv4` 命中对外分支的 `NotReady` 守卫，不进入 ARP/transmit，不误报成功；`pump`/`inject_frame`/`arp_resolve` 仍用设备严格 `validate_ready`，`LoopbackReady`（device==nullptr）下返回 `Disabled`。
- [x] 3.3 复核默认启动独立性：未启用验证、无网络配置时协议路径整体保持禁用（既有 `State::Disabled`/`SkippedNoDevice`/`SkippedInvalidConfig` 语义），默认 boot/storage/fs/`/rw`/shell/userland 行为不受影响。
  - 复核：默认 `g_default_context` 零初始化即 `State::Disabled`；normal boot 不调用 `init_default`；`SYS_SOCKET` 仍要求 `state == Ready` 否则 `ENODEV`。`LoopbackReady` 仅由显式带本机配置的 `init` 产生，默认启动不触达。

## 4. 运行期 smoke 验证

- [x] 4.1 在 `xmake/options.lua` 新增默认关闭开关 `loopback_network_smoke`（`set_default(false)`、`set_showmenu(true)`、描述其为 validation-only 本机地址 loopback 闭环 marker），遵循既有 `socket_smoke`/`network_protocol_smoke` 选项模式。
- [x] 4.2 在 `xmake/kernel.lua` 把 `loopback_network_smoke` 映射到 `BIGOS_LOOPBACK_NETWORK_SMOKE` 宏，遵循既有 smoke 宏映射模式。
- [x] 4.3 在 `kernel/core/net`（`protocol.cc` 或新增 loopback smoke 单元）实现 `#ifdef BIGOS_LOOPBACK_NETWORK_SMOKE` 内核内部闭环入口：仅以本机配置初始化 context（无帧级 `NetworkDevice`）；bind 一个本机 UDP 端口；`udp_send_to(127.0.0.1/local_ipv4, port)`；`udp_receive_from` 校验收到相同 payload、来源地址与来源端口，并断言 `loopback_delivered` 递增；覆盖未绑定、队列满（连续发满 `UDP_RX_QUEUE_CAPACITY`+1，断言 `loopback_dropped` 递增）、非本机目的（无设备下确定性失败而非误报成功）等错误路径；发 COM1 `BIGOS_LOOPBACK_NETWORK_PASSED`/`BIGOS_LOOPBACK_NETWORK_FAILED` 标记。
- [x] 4.4 在同一 smoke 入口新增本机 ICMP echo-to-self 覆盖：以本机地址（`127.0.0.1` 或 `local_ipv4`）为目的经内核内部路径发起一个合法 ICMPv4 echo request，断言经 loopback 路径产生一次 echo reply（`icmp_echo_requests`/`icmp_echo_replies` 与 `loopback_delivered` 确定性递增）、未经帧级设备发送、且 echo reply 不触发新的 echo request（递归深度 ≤1）。将断言结果并入同一 `BIGOS_LOOPBACK_NETWORK_PASSED`/`_FAILED` 标记。
- [x] 4.5 在 `kernel/core/kernel.cc` 的 smoke 阶段按既有模式以 `#ifdef BIGOS_LOOPBACK_NETWORK_SMOKE` 守卫 spawn 该入口线程（含相应头包含）；确认默认（开关关闭）构建不含该线程、默认启动行为不变。

## 5. 构建与静态检查

- [x] 5.1 运行 `xmake`（x86_64-elf-gcc 交叉构建）确认默认配置编译通过且默认启动行为不变；若交叉工具链不可用则显式记录该阻塞与残留风险。
  - 结果：`x86_64-elf-gcc 12.2.0` 可用；`xmake f --loopback_network_smoke=n && xmake` 编译通过（build ok）。
- [x] 5.2 运行 `xmake f --loopback_network_smoke=y && xmake` 确认 smoke 配置编译通过；随后 `xmake f --loopback_network_smoke=n` 复位默认配置。
  - 结果：smoke 配置编译通过（build ok），随后已复位 `--loopback_network_smoke=n`。
- [x] 5.3 对新增/修改的 C++ 文件（`include/bigos/net.h`、`kernel/core/net/protocol.cc` 及 smoke 单元）执行 clang 辅助静态检查，配置尽量贴近 GCC 交叉环境（freestanding C++17、x86_64 target、项目 include 路径、no hosted runtime、no exceptions、no RTTI）；修复本次变更引入的 clang 错误，确认或修复有效新增告警。若等价 clang 标志不可用则记录差距与残留风险。
  - 结果：`clang -std=c++17 -fsyntax-only -Wall -Wextra -ffreestanding -fno-rtti -fno-exceptions --target=x86_64-elf ...` 对 `protocol.cc`（含 `-DBIGOS_LOOPBACK_NETWORK_SMOKE`）与 `socket.cc` 均 0 错误 0 告警。
- [x] 5.4 对上述文件执行 clangd 辅助诊断；区分历史诊断、本次变更诊断与 freestanding/工具链误报，修复本次引入的问题。
  - 结果：`clangd --check` 报 “4 errors”，经核实全部为 `tweak: ExtractFunction ==> FAIL`（refactor 探测，非编译诊断，由 smoke 循环内 break/continue 触发）；以 clangd 相同 cc1 flags 运行 `clang -cc1` 产生 0 个 `error:`；同目录未改动的 `socket.cc` clangd 0 errors，无关的 `kmem.cc` 116 errors 属既有 freestanding 误报基线。本次变更未引入真实诊断。
- [x] 5.5 网络协议路径专项复核：本机过滤放宽不误接受非本机目的（`ipv4_not_local` 仍生效）；源/宿地址域归一化使校验和自洽；有界递归深度（≤1）成立；loopback 就绪不误启用整套协议栈；容量满/未绑定复用既有确定性状态；对外 RX/TX ownership 与 IRQ 边界零回归。
  - 复核：`handle_ipv4` 用 `is_local_delivery`+broadcast，非本机仍 `ipv4_not_local`；`udp_send_to`/`send_ipv4` 双侧把回环归一化为 `local_ipv4` 令伪首部校验自洽；`handle_icmp` 对 echo reply 终止（深度≤1）；`LoopbackReady` 仅放行 bind/send，`pump`/`inject_frame`/`arp_resolve` 仍设备严格；队列满/未绑定复用 `handle_udp` 既有 `QueueFull`/`NotBound`；对外 `transmit_ethernet`/`pump`/RX 归还路径与 IRQ 边界未改。

## 6. 源级契约与仿真器 smoke

- [x] 6.1 运行既有相关源契约/结构断言测试确认新增 `net.h` 回环常量与 `Diagnostics` 计数字段不破坏既有布局；若有对应 `tests/` 用例则扩展断言（本机地址常量取值、`Diagnostics`/`Context`/`UdpEndpoint` 既有字段偏移不变、新增 `loopback_delivered`/`loopback_dropped` 位于末尾），运行 `uv run pytest <目标用例>`；`uv` 或用例不适用时显式记录。本变更不新增 syscall 编号，无需 syscall 号契约新增断言。
  - 结果：新增 `tests/test_loopback_network_path_source.py`（常量取值、`Diagnostics` append-only + offset 守护、`State::LoopbackReady` 末尾、分流/归一化/就绪门/smoke/默认关闭、无新 syscall/socket ABI）；`uv run pytest tests/test_loopback_network_path_source.py tests/test_virtio_net_driver_source.py` 10 passed。
- [x] 6.2 通过 QEMU headless 路径运行 smoke（启用 `loopback_network_smoke`）并期待 COM1 `BIGOS_LOOPBACK_NETWORK_PASSED`，例如 `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial.log --expect-serial-marker BIGOS_LOOPBACK_NETWORK_PASSED`；若 QEMU/工具链/镜像不可用则显式记录跳过原因与残留风险。
  - 结果：QEMU 11.0.1 UEFI headless 运行观测到 `BIGOS_LOOPBACK_NETWORK_PASSED`（日志 `logs/loopback_smoke_serial.log`）。
- [x] 6.3 运行一次默认配置的 QEMU headless 默认启动回归（期待既有 `BIGOS_USER_EXEC` marker），确认 loopback 改动未影响默认启动进入 shell；不可用时记录跳过与残留风险。
  - 结果：`--loopback_network_smoke=n` 默认配置 QEMU UEFI headless 观测到 `BIGOS_USER_EXEC`（日志 `logs/loopback_default_boot_serial.log`）。

## 7. 文档与验证记录

- [x] 7.1 在 `docs/en` 更新网络路径说明，新增 loopback 本机地址闭环语义（本机地址集合=本机 IPv4 + `127.0.0.0/8`、IPv4 输出层分流、无 ARP/无帧级设备、无设备下 loopback 就绪、复用 UDP endpoint 有界 RX 队列与就绪模型、默认关闭 smoke 与 marker、非目标边界），并在 `docs/zh` 同步对应相对路径镜像；使用仓库相对路径，不暗示 boot/链接/向量/页表/磁盘/既有 ABI 编号变更。
  - 结果：`docs/en/arch/bounded-network-protocol-path.md` 与 `docs/zh/arch/bounded-network-protocol-path.md` 同相对路径新增 “Local-Address Loopback Path / 本机地址 Loopback 路径” 一节；`uv run pytest tests/test_bilingual_docs_layout.py` 4 passed。
- [x] 7.2 在 change 目录整理验证记录（`validation.md`），分别列出已通过的检查、因环境无法运行的检查（含原因与残留风险）、历史诊断与本次变更引入的问题。
  - 结果：新增 `openspec/changes/add-loopback-network-path/validation.md`，含改动范围、已通过检查（默认/smoke 构建、QEMU 双 marker、clang、pytest 基线 diff）、专项复核、无法独立运行项与残留风险、本次引入并已解决的问题。
