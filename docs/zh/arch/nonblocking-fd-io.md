# 有界非阻塞 fd I/O

BigOS 在统一 fd 就绪模型之上新增了一个有界 `O_NONBLOCK` 子集，使单线程用户程序
可以让读、写、接收在数据未就绪时返回确定性的 would-block 状态而非阻塞。它复用既有
就绪谓词与阻塞原语；不暗示完整 POSIX `O_NONBLOCK`、异步 I/O 或用户可见的多路复用
syscall，也不新增 syscall 编号。

## 打开文件描述粒度的非阻塞标志

- `include/bigos/fs/vfs.h` 在 `vfs::File` 末尾追加一个布尔字段 `nonblocking`
  （默认 false）。既有布局不重排；`static_assert` 偏移守卫保证 `nonblocking`
  追加在 `identity` 之后（而 `identity` 仍在 `writable` 之后）。
- 该标志存于打开文件描述（open file description），因此 `dup`/`dup2`/`fork`
  天然共享——这些路径通过 `vfs::retain` 共享同一 `vfs::File`。通过引用该打开文件
  描述的任一描述符设置标志，对其它描述符一致可见。
- 两个内核内辅助支撑各后端的 would-block 判定点与 fd-control：
  `file_is_nonblocking(File*)` 读取标志（空 file 返回 false，调用方回退到
  `!can_block()` 短路）；`file_access_mode(File*)` 由 `readable`/`writable` 合成
  `OPEN_RDONLY`/`OPEN_WRONLY`/`OPEN_RDWR` 访问模式位，供 `F_GETFL` 使用。
- `O_NONBLOCK` 的唯一内核来源是 `vfs::OPEN_NONBLOCK`（`1 << 11`），由
  `bigos::proc::O_NONBLOCK` 与用户 libc 的 `O_NONBLOCK` 镜像。该取值与 `OPEN_*`
  访问/创建标志不冲突。

## fd-control：F_GETFL / F_SETFL

- `include/bigos/proc.h` 在不变的 `FCNTL_F_DUPFD = 0` / `FCNTL_F_GETFD = 1` /
  `FCNTL_F_SETFD = 2` 之外新增 `FCNTL_F_GETFL = 3` 与 `FCNTL_F_SETFL = 4`。
- `proc::fcntl_fd_current`（`kernel/core/proc/proc.cc`）：
  - `F_GETFL` 返回合成的访问模式位，并在打开文件描述的非阻塞标志置位时按位或
    `O_NONBLOCK`。不改 fd、offset、引用、close-on-exec。
  - `F_SETFL` 为单条 `nonblocking = (arg & O_NONBLOCK) != 0`。其余所有位
    （访问模式位、创建位、任何未实现的 status 位）一律忽略且不报错、返回成功。
    这是唯一不会打挂标准惯用法 `F_SETFL(F_GETFL | O_NONBLOCK)` 的策略——该惯用法
    的实参必然携带访问模式位。它不触碰访问模式与 `FdEntry.close_on_exec`。
- `sys_fcntl`（`kernel/core/syscall/syscall.cc`）仅对 `F_DUPFD`（唯一可能增长
  fd 表/分配的命令）保留 `can_block()` guard。`F_GETFL`/`F_SETFL` 是纯标志读写，
  无需该 guard。

## 各后端 would-block 短路

各后端既有的“将要阻塞”判定点正是非阻塞短路点。统一规则：当且仅当操作需要进入
等待时，非阻塞描述符（或不可阻塞上下文）返回 would-block，且复用与阻塞路径相同
的谓词（就绪查询与 would-block 行为因此不会漂移）。

- pipe（`kernel/core/ipc/pipe.cc`）：`pipe_read` 在空管道且写端打开、`pipe_write`
  在满管道且读端打开时，把既有 `if (!can_block())` 条件扩展为
  `if (file_is_nonblocking(file) || !can_block())`，返回 `WouldBlock`。已写出
  部分字节的写返回已写字节（既有 `done > 0` 行为）而非 would-block。
- tty（`kernel/core/terminal/tty.cc`）：不可阻塞上下文保留既有严格短路。此外，
  非阻塞终端读在无可用输入时返回 `WouldBlock`，使用与 `tty_poll` 相同的
  `input_available` 谓词；不出队任何输入记录、不改输入环。终端写出方向恒可写，
  非阻塞不改变其行为。由于 fd 0/1/2 与任何 `dup` 副本共享同一终端打开文件描述，
  非阻塞标志在它们之间联动可见（POSIX 共享 OFD 语义）；需要独立阻塞性的程序应
  重新打开终端获得新的打开文件描述。
- socket（`kernel/core/syscall/syscall.cc` 的 `sys_recvfrom`）：非阻塞 socket fd
  只做一次有界 RX 推进（`recv_rounds = 1`）并跳过 `sched::yield()`；无 datagram
  返回 `-EAGAIN`。阻塞 socket fd 的有界 poll-and-yield 轮次与返回码保持不变。

`WouldBlock` 经既有 `vfs::Status` -> errno 路径映射为 `-EWOULDBLOCK`（等于
`-EAGAIN`，值 11），用户可见返回码确定。

## would-block 与就绪查询一致性契约

对同一描述符在同一时刻，`poll_file` 报告 `READY_READABLE` 当且仅当非阻塞读不会
返回 would-block；报告 `READY_WRITABLE` 当且仅当非阻塞写不会返回 would-block。
各后端通过在 would-block 判定点复用同一就绪/阻塞谓词来保证该不变式，而非各自维护
可能漂移的第二份条件。这是后续多路复用 syscall 能正确驱动非阻塞描述符的前提。

## 用户 libc 镜像

- `user/libc/include/fcntl.h` 新增 `O_NONBLOCK`（`1 << 11`，与内核同值）、
  `F_GETFL = 3`、`F_SETFL = 4`，并在头注释中说明有界 `O_NONBLOCK` 子集（非完整
  POSIX status-flag 处理）。
- `user/libc/syscall.c` 的 `fcntl` wrapper 对 `F_SETFL`（连同 `F_DUPFD`/
  `F_SETFD`）透传可变参；`F_GETFL` 不取参、传 0。wrapper 保持 freestanding-safe
  与既有 errno 翻译。

## 验证

- 默认关闭的 xmake 开关 `--nonblocking_fd_smoke=y` 映射到
  `BIGOS_NONBLOCKING_FD_SMOKE` 宏，遵循既有 smoke 选项模式。默认关闭，不改变
  默认启动行为。
- smoke 入口（`bigos::proc::nonblocking_fd_smoke_entry`，由
  `kernel/core/kernel.cc` spawn）在可阻塞内核线程上下文中通过真实 fd 表运行，
  断言：`F_GETFL`/`F_SETFL` 含访问模式的往返；`F_SETFL(F_GETFL | O_NONBLOCK)`
  携带访问模式位时仍成功且只改非阻塞位；非阻塞空管道读与非阻塞满管道写返回
  would-block，清除标志后恢复可阻塞路径；`poll_file` 可读与非阻塞读不 would-block
  一致；非阻塞 tty 无输入读返回 would-block；终端 fd 0/1/2 经 `F_GETFL` 联动可见
  非阻塞位；非阻塞已绑定 socket 在空接收队列下报告不可读。它发出确定性
  `BIGOS_NONBLOCKING_FD_PASSED` / `BIGOS_NONBLOCKING_FD_FAILED` COM1 标记，
  经 QEMU headless 路径验证。
- 内核/用户常量一致性（`O_NONBLOCK`/`F_GETFL`/`F_SETFL` 取值及与 open flag
  不冲突）由源级契约测试 `tests/test_syscall_entry_source.py` 钉死。

## 非目标

- 不实现完整 POSIX `O_NONBLOCK`：常规文件、块设备、目录维持同步完成语义，不产生
  本能力定义之外的 would-block 返回。
- 不实现 `O_ASYNC`/信号驱动 I/O、记录锁、`F_DUPFD_CLOEXEC` 或描述符传递。
- 不引入用户可见的多路复用 syscall（`poll`/`select` 类）；本变更不新增 syscall
  编号，不改动 `int 0x80` ABI、boot/链接/向量/页表/磁盘布局。
- `O_NONBLOCK` 未接入 `open()` 初始标志解析；仅经 `F_SETFL` 设置。
