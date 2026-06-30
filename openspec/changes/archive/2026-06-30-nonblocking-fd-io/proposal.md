## Why

当前所有可阻塞描述符（pipe、tty、socket）只有两种行为：在可阻塞进程上下文里阻塞等待，或在不可阻塞上下文（IRQ/调度临界区）确定性返回 `WouldBlock`。用户程序无法主动声明“不要为我阻塞”，因此要写事件循环只能逐个描述符忙等或依赖阻塞读写，违背 M13 “单线程程序一次等待多个描述符、编写事件循环”的用户可见目标。在内核已具备统一 fd 就绪（readiness）模型之后，下一步需要让读写在数据未就绪时返回确定性的 would-block 状态而非阻塞，并通过既有 fd-control 路径（`SYS_FCNTL`）开关该行为，为后续多路复用 syscall 提供可被一致驱动的非阻塞描述符。

## What Changes

- 为描述符引入有界非阻塞标志（O_NONBLOCK 语义子集）：在内核 `vfs::File`（open file description 粒度）末尾追加一个非阻塞标志位，默认关闭；通过 `dup`/`fork` 的引用计数路径自然共享，符合“同一打开文件描述共享标志”的 POSIX 语义。
- 扩展既有 fd-control 路径而不新增 syscall 编号：在 `SYS_FCNTL`（编号 48）上新增 `F_GETFL`/`F_SETFL` 两个命令，`F_GETFL` 返回访问模式位与非阻塞位的快照，`F_SETFL` 仅允许切换非阻塞位、对不支持的标志位采取确定性拒绝/忽略；新增内核内 `O_NONBLOCK` 常量，取值与既有 open flag 不冲突。
- 集成到既有阻塞读写后端：当描述符被标记为非阻塞时，pipe、tty 的读/写在“将要阻塞”的判定点直接返回 would-block（复用既有 `WouldBlock` 状态与 `read_ready`/`write_ready`/`input_available` 同源谓词），而不进入等待队列；语义与既有 `!can_block()` 短路路径一致。
- 集成到 socket recvfrom：标记为非阻塞的 socket fd 在一次有界 RX 推进后若无数据立即返回 `-EAGAIN`，跳过既有的有界 poll-and-yield 等待轮次；其它（阻塞）socket fd 维持现有有界等待语义不变。
- 用户 libc 镜像：在 `user/libc/include/fcntl.h` 增补 `O_NONBLOCK`、`F_GETFL`、`F_SETFL` 常量并让 `fcntl` wrapper 透传，保持 freestanding-safe 与既有 errno 翻译；内核与用户两份定义保持一致。
- 新增一个默认关闭的运行期 smoke 开关与 COM1 标记，验证 pipe/tty/socket 在置位 `O_NONBLOCK` 后“无数据读返回 would-block、缓冲满写返回 would-block、清除标志后恢复阻塞”等确定性行为，并验证 `F_GETFL`/`F_SETFL` 往返一致。
- 非目标：不实现完整 POSIX `fcntl`/`O_NONBLOCK` 在常规文件、块设备、目录等所有描述符类型上的语义；不实现 `O_ASYNC`/信号驱动 I/O、记录锁、`F_DUPFD_CLOEXEC` 或描述符传递；不实现面向用户态的多路复用 syscall（属于后续 M13.3）；不改动既有 syscall 编号取值、`int 0x80` ABI、boot/链接/向量/页表/磁盘布局。

## Capabilities

### New Capabilities
- `nonblocking-fd-io`: 有界非阻塞描述符行为。为 pipe、tty、socket 描述符提供 open file description 粒度的非阻塞标志，使读/写/接收在数据未就绪时返回确定性 would-block 状态（`-EWOULDBLOCK`/`-EAGAIN`）而非阻塞，复用既有 readiness 谓词与阻塞原语；不暗示完整 POSIX `O_NONBLOCK` 语义，也不覆盖常规文件/块设备等恒定就绪的描述符类型。

### Modified Capabilities
- `bounded-syscall-surface`: 既有“有界 fd 控制 primitive” requirement 当前显式声明 MUST NOT 实现 nonblocking I/O；本变更将该排除项收敛为“在 `SYS_FCNTL` 上新增 `F_GETFL`/`F_SETFL` 控制 `O_NONBLOCK` 子集”，同时保持不实现完整 POSIX `fcntl`、文件锁、async I/O、`F_DUPFD_CLOEXEC` 与描述符传递的边界。

## Impact

- 受影响内核子系统：VFS（`include/bigos/fs/vfs.h`、`kernel/core/fs/vfs.cc`，为 `vfs::File` 追加非阻塞标志并提供读写辅助）、进程 fd 控制（`include/bigos/proc.h`、`kernel/core/proc/proc.cc` 的 `fcntl_fd_current`）、syscall 分发（`kernel/core/syscall/syscall.cc` 的 `sys_fcntl`/`sys_recvfrom`）、IPC pipe（`kernel/core/ipc/pipe.cc`）、terminal/tty（`kernel/core/terminal/tty.cc`）、网络 socket（`kernel/core/net/socket.cc`）。
- ABI/接口影响：不新增 syscall 编号；在既有 `SYS_FCNTL` 上扩展命令集（`F_GETFL`/`F_SETFL`）；新增 `O_NONBLOCK` 常量需在 `include/bigos/syscall.h`/`include/bigos/proc.h` 与 `user/libc/include/fcntl.h` 两份定义保持一致并与既有 open flag 取值不冲突；`vfs::File` 仅在末尾追加一个布尔字段，不重排既有布局（以 `_Static_assert`/源级核对守护）。
- 复用而非改动既有原语：调度阻塞原语（`bigos::sched::WaitQueue`/`wait_queue_wait_until`/`can_block`）、fd readiness 模型（`vfs::poll_file` 与各后端 `poll` op 的同源谓词）、统一 errno（`EWOULDBLOCK`/`EAGAIN`，值均为 11）；非阻塞只在“将要阻塞”的判定点短路，不改变成功路径的数据搬运与唤醒逻辑。
- 构建/验证：新增一个默认关闭 xmake smoke 开关（映射到 `BIGOS_*` 宏）与对应 COM1 标记，遵循既有 smoke 模式，通过 QEMU headless 路径验证；不改动默认启动行为。用户/内核 syscall number 与 fcntl 常量一致性沿用既有源级契约校验（`uv run pytest`）。
- 架构/内存/仿真器/工具链假设：x86_64-only、UEFI 默认 backend；freestanding-safe，无 hosted libc/异常/RTTI；沿用 `x86_64-elf-gcc` 交叉工具链与 xmake；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
