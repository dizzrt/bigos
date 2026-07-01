## Why

内核已具备统一 fd 就绪（readiness）模型（`vfs::poll_file` 对 pipe/socket/tty 返回可读/可写/错误位）与有界非阻塞描述符行为（`F_SETFL` 开关 `O_NONBLOCK`），但用户程序仍无法“一次等待多个描述符”。当前要监听多个 fd 只能对每个 fd 轮流非阻塞探测并自旋忙等，既浪费 CPU 又无法带超时地阻塞让出。这正是 M13“单线程程序一次等待多个描述符、编写事件循环”用户可见目标尚缺的最后一环：需要一个有界的多路复用 syscall，对定容描述符集合带超时等待并报告各描述符就绪状态，让线程在无 fd 就绪时真正睡下、任一就绪或超时到期再醒来。

## What Changes

- 新增一个用户可见的有界多路复用 syscall（poll(2) 风格）：接受一个定容的 pollfd 数组（每项含 fd、请求关注的就绪位、返回的就绪位）与一个毫秒级超时，阻塞等待直到集合中至少一个描述符就绪、超时到期、或确定性错误，返回就绪描述符个数（超时返回 0）。
- ABI 追加而不改号：在既有 `int 0x80` surface 末尾追加 `SYS_POLL = 61`（当前最大编号为 `SYS_DYN_PROTECT = 60`），沿用既有寄存器传参约定与“追加新号、不动既有号”的 syscall 契约；不改动任何既有 syscall 编号、寄存器顺序、`int 0x80` 返回语义、syscall gate DPL 与 no-EOI 规则。
- 就绪判定完全复用统一就绪模型：对每个被监听 fd 调用既有 `vfs::poll_file(File*)`，把内核内 `READY_READABLE`/`READY_WRITABLE`/`READY_ERROR` 位映射到用户可见的 poll 事件位；就绪判定与非阻塞读写、阻塞谓词三者同源，不新增各自维护、可能漂移的判定。
- 复用调度等待队列实现真正阻塞：扩展调度器阻塞原语，使一个线程可以同时挂到被监听 fd 各自后端的就绪等待队列（pipe 读/写等待、socket `rx_wait`、tty 输入等待）上，任一就绪 `wake_*` 或超时 tick 到期即唤醒；唤醒后重扫 `poll_file` 汇总就绪集合。这是本变更对调度层的主要新增能力。
- 用户 libc 镜像：在 `user/libc` 增补 poll 相关常量（`POLLIN`/`POLLOUT`/`POLLERR` 等有界子集）、`struct pollfd` 布局与 `poll` wrapper，透传到 `SYS_POLL` 并保持 freestanding-safe、既有 errno 翻译；内核与用户两份 pollfd 布局/事件位定义保持一致。
- 新增一个默认关闭的运行期 smoke 开关与 COM1 标记，验证：无 fd 就绪时带超时阻塞并在超时后返回 0；某 fd 变就绪后唤醒并只报告该 fd 的就绪位；就绪位与 `poll_file`/非阻塞读写一致；定容/非法参数/坏 fd 的确定性错误行为。

## Capabilities

### New Capabilities
- `bounded-fd-multiplexing`: 有界多路复用 syscall。对一个定容的描述符集合带毫秒超时等待，复用统一就绪模型报告各描述符可读/可写/错误就绪位，复用调度等待队列在无就绪时真正阻塞、任一就绪或超时唤醒；保持有界，不声称完整 POSIX `poll`/`select`/`epoll` 语义、不支持无界描述符集合、不支持信号中断的复杂 restart 语义、不引入用户可见的边缘触发/事件通知对象。

### Modified Capabilities
- `bounded-syscall-surface`: 在有界 syscall surface 末尾追加 `SYS_POLL` 号（append-only），扩展“用户可见有界 syscall 契约”以包含多路复用消费面，同时保持既有编号、寄存器顺序、`int 0x80` 返回与 no-EOI 规则不变，并明确不实现完整 POSIX 多路复用族。
- `kernel-blocking-primitives`: 扩展调度器阻塞原语，使一个等待线程可在同一次阻塞中同时登记到多个就绪等待队列上、由任一队列的 `wake_*` 或超时 tick 唤醒，且登记/注销在 IRQ-safe、分配无关的约束内完成；不改变既有单队列 `wait_queue_wait_until`/`wake_*` 语义。

## Impact

- 受影响内核子系统：syscall 分发（`kernel/core/syscall/syscall.cc` 新增 `sys_poll` 与分发分支）、syscall ABI 头（`include/bigos/syscall.h` 追加 `SYS_POLL` 与 pollfd/事件位定义）、调度器（`include/bigos/sched.h`、`kernel/core/sched/sched.cc` 新增多队列等待原语）、VFS 就绪查询复用（`include/bigos/fs/vfs.h`、`kernel/core/fs/vfs.cc` 的 `poll_file`，只读复用不改语义）、进程 fd 解析（`kernel/core/proc/proc.cc` 的 `file_for_fd_current` 复用）。
- ABI/接口影响：仅 append-only 新增 `SYS_POLL = 61`；新增 `struct pollfd` 与 `POLLIN`/`POLLOUT`/`POLLERR` 等事件位常量，需在 `include/bigos/syscall.h` 与 `user/libc` 两份定义保持一致且与既有常量不冲突。ABI 约束原则上“优先复用现有 syscall”，但多路复用是全新消费面，poll/select 类操作无可自然复用的既有 syscall（不同于 `O_NONBLOCK` 可挂在 `SYS_FCNTL`），故采用与 `SYS_SOCKET..SYS_DYN_PROTECT` 一致的“追加新号”做法；不动既有号、寄存器顺序与 `int 0x80` 语义。
- 复用而非改动既有原语：统一就绪查询 `vfs::poll_file` 与各后端 `poll` op、非阻塞标志 `file_is_nonblocking`、调度等待/唤醒/超时（`WaitQueue`/`wake_one`/`wake_all`/超时 tick）、用户指针校验（`validate_user_io_buffer`/`copy_*_user_buffer`）、统一 errno；多队列等待是对既有单队列原语的追加式扩展，成功路径的数据搬运与唤醒逻辑不受影响。
- 构建/验证：新增一个默认关闭 xmake smoke 开关（映射到 `BIGOS_*` 宏）与对应 COM1 标记，遵循既有 smoke 模式，通过 QEMU headless 路径验证；不改动默认启动行为。内核/用户 syscall 号与 poll 常量/pollfd 布局一致性沿用既有源级契约校验（`uv run pytest`）。
- 非目标：不实现完整 POSIX `poll`/`select`/`epoll`/`ppoll`；不支持无界或动态增长的描述符集合（集合定容、超出即确定性拒绝）；不实现边缘触发、事件通知对象（eventfd/epoll fd）、`POLLPRI`/带外数据、`POLLRDHUP` 等扩展事件；不实现被信号中断后的 `-EINTR` 复杂 restart 语义（信号交互保持既有阻塞原语的既定行为）；不把多路复用引入常规文件/块设备（这些按就绪模型的确定性默认恒可读可写）；不改动既有 syscall 编号、`int 0x80` ABI、boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。
- 架构/内存/仿真器/工具链假设：x86_64-only、UEFI 默认 backend；freestanding-safe，无 hosted libc/异常/RTTI；沿用 `x86_64-elf-gcc` 交叉工具链与 xmake；QEMU headless 为主要 smoke 路径；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
