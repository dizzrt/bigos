## Why

当前内核为 pipe 与 tty 各自实现了一套就绪判断与等待队列（pipe 的 `read_ready`/`write_ready`，tty 的 `input_available`），而 UDP socket 的接收路径没有等待队列，`sys_recvfrom` 依赖一段“poll-and-yield”轮询。这意味着内核缺少一个统一、可被复用的 fd 就绪（readiness）模型：上层无法一致地查询某个描述符是否可读、可写或处于错误状态。后续的非阻塞描述符行为与多路复用 syscall 都需要先有这样一个统一模型作为基础，因此现在先把就绪语义在内核内统一并补齐 socket 的等待队列。

## What Changes

- 在 VFS 层引入统一的内核 fd 就绪查询能力：为 `vfs::File` 增加一个可选的就绪（readiness）操作（在 `FileOperations` 末尾追加，不重排既有槽位），用一组位标志表达可读、可写、错误三类就绪状态。
- 为 pipe 后端接入就绪查询：复用既有 `read_ready`/`write_ready` 判断，把写端关闭后的可读 EOF、读端关闭后的写端错误（broken pipe）映射为对应就绪位。
- 为 socket（有界 UDP）后端补齐等待队列：在 UDP endpoint/socket 上新增接收等待队列，由协议 RX 投递路径在数据到达时唤醒；并提供就绪查询，把“收队列非空”表达为可读、“可发送”表达为可写、端点失活表达为错误。
- 为 terminal/tty 描述符接入就绪查询：把全局 tty 输入环与其等待队列桥接到 fd 就绪查询路径，复用既有 `input_available`/`input_record_available` 判断，使终端描述符的可读就绪可被统一查询。
- 新增一个默认关闭的运行期 smoke 开关与 COM1 标记，验证三类描述符（pipe、socket、tty）在有数据/可写/关闭等条件下报告的就绪位与既有阻塞行为一致。
- 非目标：不实现非阻塞读写返回 would-block 的描述符标志（属于后续 M13.2），不实现面向用户态的多路复用 syscall（属于后续 M13.3），不暗示完整 POSIX `poll`/`select`/`epoll` 语义。

## Capabilities

### New Capabilities
- `fd-readiness-model`: 内核内统一的 fd 就绪查询模型，为 socket、pipe、terminal 描述符在既有阻塞原语与调度等待队列之上表达可读、可写、错误三类就绪状态，并为 socket 接收路径补齐等待队列；不引入用户态多路复用 syscall，也不暗示完整 POSIX poll 语义。

### Modified Capabilities
<!-- 本变更通过追加可选 readiness 操作扩展 VFS/pipe/socket/tty 的内核内行为，不修改既有 capability 的对外 spec 级 requirement；相关行为以新 capability 表达。 -->

## Impact

- 受影响内核子系统：VFS（`kernel/core/fs`、`include/bigos/fs/vfs.h`）、IPC pipe（`kernel/core/ipc`、`include/bigos/ipc/pipe.h`）、网络 socket/UDP（`kernel/core/net`、`include/bigos/net/socket.h`、`include/bigos/net.h`）、terminal/tty（`kernel/core/terminal`、`include/bigos/tty.h`）、进程 fd 表（`kernel/core/proc`、`include/bigos/proc.h`）。
- 复用而非改动调度阻塞原语：`bigos::sched::WaitQueue`、`wait_queue_wait_until`、`wake_one`/`wake_all`、`can_block()`（`include/bigos/sched.h`、`kernel/core/sched/sched.cc`）；新增的 socket 等待队列遵循既有 IRQ-safe 唤醒约定。
- ABI/接口影响：`FileOperations` 仅在末尾追加一个可选函数指针，不重排既有槽位；本变更不新增 syscall 编号、不改动 syscall ABI（用户可见的多路复用 syscall 延后到 M13.3）。
- 构建/验证：新增一个默认关闭的 xmake smoke 开关（映射到 `BIGOS_*` 宏）与对应 COM1 标记，遵循既有 smoke 模式，通过 QEMU headless 路径验证；不改动默认启动行为。
- 架构/内存/仿真器/工具链假设：x86_64-only、UEFI 默认 backend；freestanding-safe，无 hosted libc/异常/RTTI；沿用 `x86_64-elf-gcc` 交叉工具链与 xmake；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
