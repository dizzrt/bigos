## Context

当前内核的描述符就绪（readiness）能力分散且不一致：

- pipe（`kernel/core/ipc/pipe.cc`）已有 `read_ready`/`write_ready` 两个判断与 `read_wq`/`write_wq` 两个等待队列，阻塞读写在空/满时通过 `wait_queue_wait_until` 等待，并在状态变化时 `wake_all` 唤醒对端。
- tty（`kernel/core/terminal/tty.cc`）已有 `input_available`/`input_record_available` 判断与全局 `g_input_wait` 等待队列，IRQ 上下文的输入入队路径通过 `wake_one` 唤醒阻塞读者；但 tty 当前是全局单例，尚未表达为 `vfs::File`/fd 对象。
- UDP socket（`kernel/core/net/socket.cc`、`kernel/core/net/protocol.cc`）的 `UdpEndpoint` 只有一个有界接收队列 `rx_queue`，**没有等待队列**；`sys_recvfrom`（`kernel/core/syscall/syscall.cc`）通过有界的 `net::pump` + `udp_receive_from` + `sched::yield()` 轮询数据。

描述符类型的区分依赖 ops 指针标识（`is_pipe_file`、`is_socket_file`），而非类型 tag。`vfs::File` 持有 `const FileOperations *ops`，`FileOperations`（`include/bigos/fs/vfs.h`）当前包含 `read/close/write/lseek/truncate/readdir`。

调度阻塞原语（`include/bigos/sched.h`、`kernel/core/sched/sched.cc`）已提供本变更需要复用的全部能力：`WaitQueue`、`wait_queue_wait_until`、`wake_one`/`wake_all`、以及决定“可否阻塞”的 `can_block()`。错误码统一在 `include/bigos/errno.h`，`EAGAIN`/`EWOULDBLOCK` 已存在（值 11）；VFS 层另有数值与负 errno 对齐的 `vfs::Status`。

本变更为 roadmap 深度阶段 M13 的第一步，目标是把上述分散能力收敛为一个统一的内核 fd 就绪查询模型，并补齐 socket 缺失的等待队列，使后续的非阻塞描述符行为与多路复用 syscall 有一致的内核基础。

## Goals / Non-Goals

**Goals:**

- 在 VFS 层提供统一的内核内就绪查询入口，对一个 `vfs::File` 返回一组就绪位标志：可读（readable）、可写（writable）、错误（error）。
- 用一组稳定的位标志常量表达就绪状态，定义为内核内部约定（不进入用户 ABI）。
- pipe、socket、tty 三类描述符均可通过统一入口被查询，且查询结果与各自既有的阻塞/唤醒行为一致（即：查询报告“可读”当且仅当对应阻塞读不会再阻塞）。
- 为 UDP socket 的 `UdpEndpoint`/`Socket` 补齐接收等待队列，由协议 RX 投递路径在数据到达时 `wake_*`，保持 IRQ-safe 与 freestanding-safe。
- 通过一个默认关闭的运行期 smoke（COM1 标记）验证三类描述符在有数据/可写/对端关闭等条件下报告的就绪位正确。

**Non-Goals:**

- 不实现非阻塞读写标志（`O_NONBLOCK`/`F_GETFL`/`F_SETFL`）使读写返回 would-block——属于后续 M13.2。
- 不实现面向用户态的多路复用 syscall（`poll`/`select` 类）——属于后续 M13.3；本变更不新增 syscall 编号、不改动 syscall ABI。
- 不暗示完整 POSIX `poll`/`select`/`epoll` 语义，不覆盖常规文件、块设备、动态链接等其它描述符类型的“真实就绪”语义（常规文件可定义为恒可读/可写的确定性默认）。
- 不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。

## Decisions

### 决策一：在 `FileOperations` 末尾追加可选 `poll` 操作，按 ops 分发就绪查询

为 `vfs::FileOperations` 在**末尾追加**一个可选函数指针（暂命名 `poll`/`readiness`），签名形如 `uint32_t (*poll)(File *file) noexcept`，返回就绪位标志的按位或。VFS 提供统一入口 `vfs::poll_file(File*)`（或同义命名）：若 `ops->poll` 非空则调用之，否则返回确定性默认（常规文件视为可读且可写、无错误）。

- 备选 A：在 `vfs::File` 增加类型 tag 并在统一入口 `switch`。否决：与既有“以 ops 指针标识类型”的约定（`is_pipe_file`/`is_socket_file`）不一致，且需要改动所有创建点。
- 备选 B：在统一入口里用 `is_pipe_file`/`is_socket_file` 等谓词逐类分发。否决：把 net/ipc/tty 的耦合上移到 VFS，不如让各后端各自实现 `poll` op 内聚。
- 选择追加 op 的理由：遵循 `FileOperations` “只追加不重排”的既有约定；读取就绪是纯查询，不阻塞、不消费数据；常规文件无需实现即获得确定性默认。

### 决策二：就绪位标志定义为内核内部常量，与既有判断对齐

定义内核内部就绪位（如 `READY_READABLE`/`READY_WRITABLE`/`READY_ERROR`），不进入用户 ABI（用户可见编码留待 M13.3 定义）。各后端 `poll` 的语义直接复用既有判断，确保“查询可读 ⇔ 阻塞读不会阻塞”：

- pipe：`count > 0 || !write_open` → 可读（含写端关闭的 EOF 可读）；`count < CAPACITY || !read_open` → 可写；`!read_open` 时写端置错误位（broken pipe 倾向）。复用 `read_ready`/`write_ready`。
- socket（UDP）：`rx_count > 0` → 可读；端点处于可发送的有效状态 → 可写；端点失活/未绑定等不可用状态 → 错误位。
- tty：复用 `input_available`/`input_record_available` → 可读；终端写出始终可写；输入不产生错误位。

`poll` 必须是纯查询：只读快照，不出队、不阻塞、不改变 open 标志。

### 决策三：为 UDP socket 补齐接收等待队列

在 `UdpEndpoint`（或 `Socket`）上新增一个 `sched::WaitQueue rx_wait`。协议 RX 投递路径（`net::pump`/`inject_frame` → UDP demux，`kernel/core/net/protocol.cc`）在把 datagram 放入 `rx_queue` 后调用 `sched::wake_all(&rx_wait)`（或 `wake_one`）。该唤醒发生在去往 endpoint 的投递点，遵循既有 IRQ-safe 唤醒约定（`wake_*` 可在 IRQ/投递上下文调用，`wait_queue_wait_until` 仅在可阻塞线程上下文调用）。

- 本变更**保持 `sys_recvfrom` 的对外行为不变**：仍可走既有 poll-and-yield 路径，新增等待队列首先服务于统一就绪模型与后续多路复用；是否把 `sys_recvfrom` 改为基于等待队列阻塞，作为有界、可选的内聚改造，不改变其错误码与返回语义（无数据且不可阻塞仍返回 `-EWOULDBLOCK`）。
- 备选：不加等待队列，统一就绪入口对 socket 只做一次性快照查询。否决：没有唤醒源，后续多路复用将退化为忙等，违背 M13 “可写事件循环、避免逐个忙等”的用户可见目标。

### 决策四：把全局 tty 桥接为可被就绪查询的描述符

tty 当前是全局单例且未表达为 `vfs::File`。本变更提供一个最小桥接：允许就绪查询入口在识别到“终端描述符”时，读取全局 `g_input` 的就绪判断与 `g_input_wait`。实现上既可以让标准输入对应的 `vfs::File` 提供一个 tty `poll` op（首选，内聚于统一入口），也可以在统一入口内对终端 fd 做受限识别。范围限定为读就绪桥接，不引入完整 tty-as-File 改造（保持有界）。

### 控制流 / 数据流（跨 IRQ 边界）

就绪查询（同步、线程上下文，无阻塞）：

```
caller -> vfs::poll_file(File*) -> ops->poll(File*) -> 后端读取就绪快照(pipe/socket/tty) -> 返回 READY_* 位或
```

就绪状态变化的唤醒（生产侧，可能在 IRQ/投递上下文）：

```
keyboard IRQ       -> tty enqueue_input*   -> wake_one(&g_input_wait)
pipe 读/写/关闭     -> 修改 count/open 标志  -> wake_all(对端 wq)
net RX 投递         -> udp 入 rx_queue      -> wake_all(&endpoint.rx_wait)
```

等待侧（仅在 `can_block()` 为真的线程上下文）继续复用既有 `wait_queue_wait_until(predicate, ...)`，谓词与 `poll` op 的就绪判断保持同源，避免“查询说就绪、阻塞却仍等待”的不一致。

## Risks / Trade-offs

- [就绪谓词与阻塞谓词不同源导致语义漂移] → 各后端的 `poll` op 与既有 `*_ready`/`input_available` 判断必须复用同一函数或同一条件，禁止各写一份；在 smoke 中交叉验证“查询可读后阻塞读立即返回”。
- [socket 新增等待队列引入 IRQ 唤醒竞态/丢唤醒] → 严格遵循既有模式：先入队 `rx_queue` 再 `wake_*`；等待侧在 IRQ 关闭下检查谓词后入队（`wait_queue_wait_until` 既有实现已闭合 missed-wakeup 竞态）；`wake_*` 不分配内存、可在投递/IRQ 上下文调用。
- [`poll` op 误消费数据或阻塞] → 约定 `poll` 为纯只读快照，不出队、不阻塞、不改 open 标志；代码评审与 smoke 双重保证。
- [`FileOperations` 布局变化影响既有后端] → 仅在末尾追加指针并允许为空，既有后端零改动即获得确定性默认；新增字段以 `_Static_assert`/源级核对避免被错误重排。
- [tty 桥接扩大全局耦合] → 限定为读就绪只读桥接，不做完整 tty-as-File 改造，保持有界并集中在统一入口/单个 tty `poll` 实现内。
- [对 `sys_recvfrom` 行为的潜在改动] → 默认保持其对外语义不变；若改为基于等待队列阻塞，需保证无数据且不可阻塞仍返回 `-EWOULDBLOCK`，并在 smoke 中回归。

## Migration Plan

1. VFS：在 `FileOperations` 末尾追加可选 `poll` 指针，新增 `vfs::poll_file` 统一入口与默认行为；定义内核内就绪位常量。
2. pipe：实现 pipe `poll` op，复用 `read_ready`/`write_ready` 与 open 标志映射就绪/错误位。
3. socket/UDP：在 `UdpEndpoint`/`Socket` 增加 `rx_wait`；RX 投递路径数据到达后 `wake_*`；实现 socket `poll` op。
4. tty：实现 tty 读就绪桥接（首选 tty `poll` op），复用 `input_available`/`input_record_available`。
5. 验证：新增默认关闭 xmake smoke 开关 → `BIGOS_*` 宏 → 在 `kernel.cc` 增加 smoke 入口线程，对三类描述符断言就绪位并发 COM1 PASSED/FAILED 标记；通过 QEMU headless 验证。

回滚策略：本变更为追加式、默认关闭 smoke、不改默认启动路径与 ABI；如需回滚，移除 `poll` op 与 socket 等待队列、还原统一入口即可，不影响既有 pipe/tty 阻塞行为与 `sys_recvfrom` 对外语义。

## Open Questions

- 就绪位常量的内核内命名与归属头文件（`include/bigos/fs/vfs.h` vs 新增轻量头），是否需要为 M13.3 的用户可见编码预留映射？倾向：先放 VFS 头作为内核内约定，用户编码在 M13.3 再定义映射。
- 本变更是否同步把 `sys_recvfrom` 切换为基于 `rx_wait` 阻塞，还是仅补齐等待队列、保留既有 poll-and-yield？倾向：本变更只补齐等待队列与就绪查询，`sys_recvfrom` 行为改造作为可选项并保证对外语义不变。
- tty 读就绪桥接是否需要覆盖“标准输入以外的终端 fd”？倾向：限定为当前终端输入路径，保持有界。
