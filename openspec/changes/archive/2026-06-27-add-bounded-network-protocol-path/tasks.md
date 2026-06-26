## 1. 协议模块边界与构建接入

- [x] 1.1 盘点现有 `virtio-net-driver`、`bigos::device::NetworkDevice`、kernel 初始化顺序和默认关闭 smoke 接入点，确认协议层只依赖 frame-level 内核设备接口。
- [x] 1.2 新增 `bigos::net` 内核内部模块结构、最小公开头和 xmake 构建接入，保持 freestanding C++17、无异常、无 RTTI、无 hosted runtime 假设。
- [x] 1.3 定义 boot-time kernel option 解析边界、协议配置、诊断、状态码、IPv4/MAC/MTU 表示和单接口 context，缺少 ready 网络设备或启动期静态配置时进入 disabled/skipped 状态。
- [x] 1.4 审查启动地址、链接地址、页表布局、磁盘布局、IDT/syscall vector 和 fd/VFS ABI，确认本变更不修改这些边界。

## 2. 以太网与 ARP 路径

- [x] 2.1 实现 Ethernet II RX 分发和 TX 构造，验证 header 长度、目的 MAC、ethertype、MTU/buffer 边界和 RX buffer ownership。
- [x] 2.2 实现 bounded ARP cache、pending resolution、ARP request/reply、超时和容量耗尽状态。
- [x] 2.3 实现 malformed/unsupported ARP 输入拒绝，避免未验证 sender 状态污染 cache。
- [x] 2.4 审查 Ethernet/ARP 路径的 allocation phase、对象生命周期、alignment、错误返回和 RX buffer exactly-once 归还。

## 3. IPv4 与 ICMP 路径

- [x] 3.1 实现无分片 IPv4 输入验证，覆盖 version、IHL、total length、checksum、local/broadcast destination、protocol 和 bounds。
- [x] 3.2 实现 IPv4 输出构造，覆盖 TTL、protocol、source/destination、checksum、MTU too-large 状态和 ARP resolved MAC 交付。
- [x] 3.3 实现 ICMPv4 echo request/reply 的 checksum、identifier、sequence 和 bounded payload 处理。
- [x] 3.4 对 IPv4 分片、unsupported protocol、invalid checksum、truncated payload 和非本机目的地址建立确定性丢弃/诊断路径。

## 4. UDP 内核内部接口

- [x] 4.1 实现 bounded UDP endpoint/bind table，处理端口范围、重复绑定、表满和 endpoint 生命周期。
- [x] 4.2 实现 UDP send-to 内核接口，构造 UDP/IPv4/Ethernet header，处理 checksum、payload length、ARP unresolved、no-route、too-large、timeout 和 device TX failure。
- [x] 4.3 实现 UDP receive-from 内核接口和每端口 bounded RX datagram queue，记录 source IPv4、source port、payload length 和 payload bytes。
- [x] 4.4 实现 unbound port、queue full、invalid UDP length/checksum、payload overflow 的确定性丢弃与诊断。
- [x] 4.5 确认 UDP 接口不暴露用户缓冲、fd 对象、syscall、POSIX socket flag、errno 映射或 libc socket 表面。

## 5. IRQ 边界与协议推进

- [x] 5.1 实现非 IRQ 上下文的协议 pump 或 bounded validation progression，确保 virtio-net MSI-X handler 只更新 frame-level RX/TX 完成状态。
- [x] 5.2 为 ARP resolution、IPv4 dispatch、ICMP、UDP bind/send/receive 和协议 pump 增加 IRQ-context 拒绝或诊断路径。
- [x] 5.3 审查中断安全、重入、hardware access ordering、LAPIC/i8259 EOI 边界和可见失败路径，确认协议层不从 IRQ 分配、阻塞、等待或访问 VFS。
- [x] 5.4 审查 device TX stale completion、timeout、generation/ownership 与协议返回状态的关系，避免迟到完成覆盖协议失败。

## 6. 默认关闭验证与工具接入

- [x] 6.1 新增默认关闭 build switch 和协议 smoke 入口，覆盖初始化、ARP、IPv4、ICMP echo、UDP send/receive、错误丢弃和默认启动独立性。
- [x] 6.2 若需要新增或修改 Python host-side 验证辅助，使用 `uv run ...` 路径，并补充 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest` 验证；若未改 Python，记录不适用。
- [x] 6.3 复用现有 virtio-net host 辅助的 QEMU/tap 启动与清理能力，并新增协议级受控 packet injection/断言来跑协议 smoke；若 QEMU、tap 权限、MSI-X、串口捕获、host 辅助、协议包注入或 x86_64-elf 工具链不可用，记录跳过项与剩余风险。
- [x] 6.4 跑默认启动回归，确认未启用协议 smoke 或无网络后端时，storage、filesystem、`/rw`、shell 和 userland baseline 不依赖网络。
- [x] 6.5 可用时跑 Bochs 默认启动交叉验证；若 Bochs ROM/display/磁盘镜像路径不可用，记录无法运行原因和风险。

## 7. 静态检查、构建与文档收尾

- [x] 7.1 运行 xmake 目标构建，使用 x86_64-elf-gcc/x86_64-elf-g++；若交叉工具链或 xmake 不可用，记录 blocker、替代检查和剩余风险。
- [x] 7.2 运行接近 GCC 交叉构建环境的 clang 辅助检查：freestanding C++17、x86_64 target、项目 include、no exceptions、no RTTI；修复当前变更新增有效诊断，历史诊断和 false positive 分开记录。
- [x] 7.3 运行对应 clangd 辅助诊断或记录 clangd flags/config 差距；修复当前变更新增有效诊断，历史诊断和 false positive 分开记录。
- [x] 7.4 更新必要的 docs/en 与 docs/zh 镜像文档，保持 bounded network protocol path 的能力描述不宣称完整网络栈或用户态 socket 完成。
- [x] 7.5 实现完成后更新 `roadmap.md` 中 M11.2 的完成状态，保持 roadmap 仅包含项目规划级描述，不加入入口点、命令、marker、文件路径或源码细节。
- [x] 7.6 运行 OpenSpec 校验与状态检查，确认 change artifacts、规格和任务处于可归档状态，并在验证记录中区分已通过、无法运行、历史诊断和当前变更新问题。

## 验证记录

- 通过：`xmake f --network_protocol_smoke=y && uv run python -m tools.bigosdev run --boot-mode uefi --emulator qemu --display none --serial-log logs/network-protocol-smoke.serial.log --expect-serial-marker BIGOS_NETWORK_PROTOCOL_PASSED --smoke-timeout 80`。
- 通过：`xmake f --network_protocol_smoke=n && xmake`。
- 通过：默认启动回归 `uv run python -m tools.bigosdev run --boot-mode uefi --emulator qemu --display none --serial-log logs/default-init-regression.serial.log --expect-serial-marker BIGOS_USER_EXEC --smoke-timeout 80`。
- 通过：`uv run ruff check tools/bigosdev tools/virtio_net_tap.py`。
- 通过：`uv run ruff format --check tools/bigosdev tools/virtio_net_tap.py`。
- 通过：`uv run pyright tools/bigosdev tools/virtio_net_tap.py`。
- 通过：`clang++ --target=x86_64-unknown-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -Iinclude -Icpp/include -Icpp/libsupc++/include -DBIGOS_USER_PROCESS -DBIGOS_NETWORK_PROTOCOL_SMOKE -fsyntax-only kernel/core/net/protocol.cc kernel/core/kernel.cc`。
- 通过：`openspec validate add-bounded-network-protocol-path --strict`。
- 通过：`openspec status --change "add-bounded-network-protocol-path" --json` 返回 schema `spec-driven` 且 artifacts 均为 `done`。
- 阻塞：`uv run python -m tools.bigosdev smoke matrix --case bounded-network-protocol --output logs/runtime-smoke-network-protocol.md --serial-log-dir logs/runtime-smoke-network-protocol --image-dir build/test/runtime-smoke-network-protocol` 在当前 macOS 主机记录为 `blocked`，原因是 TAP-backed virtio-net smoke 需要 Linux `/dev/net/tun` 与 `tools/virtio_net_tap.py prepare`；替代证据为直接 QEMU 协议 marker、构建、静态检查和矩阵 artifact。
- 未运行：Bochs 默认启动交叉验证。本变更不修改 boot、IDT、EOI、端口 IO、磁盘布局或 ATA PIO；已用 QEMU UEFI 默认启动回归替代，剩余风险为未取得 Bochs ROM/display/backend 证据。
- 历史失败：`uv run pytest` 为 326 passed / 19 failed。失败集中在既有 source-string 断言、已归档/旧 active change 路径、历史 VM/proc/syscall 文本匹配等，与本次新增 `bigos::net`、xmake smoke、docs 或 host-side network case 无直接关系；未按本变更范围修改这些历史断言。
