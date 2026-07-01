# 有界 fd 多路复用（SYS_POLL）

BigOS 在统一 fd 就绪模型与调度等待队列之上新增了一个有界 `poll(2)` 风格的多路复用
syscall，使单线程用户程序可以对一个定容描述符集合带毫秒超时等待并编写事件循环。它
复用既有就绪快照（`vfs::poll_file`）与阻塞原语；不暗示完整 POSIX
`poll`/`select`/`epoll`/`ppoll`、无界描述符集合、边缘触发或事件对象语义，也不实现
被信号中断的复杂 restart。

## 追加式 ABI：SYS_POLL = 61

- `include/bigos/syscall.h` 末尾追加 `SYS_POLL = 61`（此前最大为
  `SYS_DYN_PROTECT = 60`）。不改动任何既有 syscall 编号、寄存器参数顺序、
  `int 0x80` 返回约定、syscall gate DPL 或异常/IRQ no-EOI 规则。
- 寄存器约定沿用固定 ABI：`rdi = 用户 pollfd*`、`rsi = nfds`、
  `rdx = timeout_ms`；`rax` 返回就绪描述符个数（超时且无就绪为 0）或负 errno。
- 用户可见结构（内核 `bigos::sys::pollfd` 与 `user/libc` 镜像保持一致）：

  ```
  struct pollfd {
      int32_t  fd;       // 被监听描述符；负值表示忽略该项
      uint16_t events;   // 请求关注的事件位（POLLIN/POLLOUT 子集）
      uint16_t revents;  // 内核回填的就绪位（含 POLLERR/POLLHUP/POLLNVAL）
  };
  ```

- 事件位（取值与 Linux 常见 `poll(2)` 对齐，便于移植）：`POLLIN = 0x001`、
  `POLLOUT = 0x004`、`POLLERR = 0x008`、`POLLHUP = 0x010`、`POLLNVAL = 0x020`。
  `POLLERR`/`POLLHUP`/`POLLNVAL` 是输出专用：即使 `events` 未请求，内核也会在对应
  条件下回填。
- `nfds` 上限为 `POLL_MAX_FDS`（16）。`nfds > POLL_MAX_FDS` 返回 `-EINVAL`；
  `nfds == 0` 合法（退化为纯超时等待）。
- `timeout_ms` 语义：`> 0` 带毫秒超时阻塞；`== 0` 只做一次就绪扫描并立即返回
  （非阻塞探测）；`< 0` 无限等待。毫秒到 tick 换算复用 `SYS_SLEEP_MS` 使用的
  粗粒度 monotonic tick。

## 复用 poll_file 的 level-triggered 就绪

对每个 `fd >= 0`，`proc::poll_fds_current`（`kernel/core/proc/proc.cc`）用
`file_for_fd_current` 解析描述符并调用 `vfs::poll_file(File*)`，把内核内 `READY_*`
位映射为用户可见 `revents`：

- `READY_READABLE` -> `POLLIN`（仅当 `events & POLLIN`）
- `READY_WRITABLE` -> `POLLOUT`（仅当 `events & POLLOUT`）
- `READY_ERROR` -> 无条件回填 `POLLERR | POLLHUP`（对端关闭 / 错误）

该映射是 level-triggered：条件持续满足就持续报告。就绪查询与非阻塞读写、阻塞谓词
三者同源，所以“poll 报可读”当且仅当非阻塞读不返回 would-block。扫描是只读的：
不出队、不改任何描述符 offset 或打开状态。

## 坏描述符与负描述符

- 非法/未打开的 `fd` 在该项回填 `POLLNVAL` 并计入就绪，不使整调用失败（与
  `poll(2)` 一致）。
- 负 `fd` 项被忽略：其 `revents` 清零，既不计入就绪也不参与阻塞。

## 调度器多队列注册阻塞

当无就绪、`timeout_ms != 0` 且上下文可阻塞时，`poll_fds_current` 收集各有效描述符
的等待队列并阻塞：

- `vfs::file_poll_wait_queues` 派发到追加的只读 `FileOperations::poll_wait` op。
  pipe 返回 `read_wq`（读端）或 `write_wq`（写端）；socket 返回端点的
  `rx_wait`；tty 返回共享的全局输入等待队列。未实现该 op 的后端（如常规文件）不
  贡献队列——因为 `poll_file` 报告其恒就绪。
- 收集的队列有界去重（线性扫描，上界 `sched::POLL_MAX_WAIT_QUEUES = 32`）后交给
  新调度原语 `sched::wait_queue_wait_any(queues, count, predicate, arg, timeout_ticks)`。
- `wait_queue_wait_any`（`kernel/core/sched/sched.cc`）是对单队列等待路径的追加式
  扩展：
  - `WaitQueue` 末尾追加 `poll_head` 链表头（既有 `head`/`tail`/`lock` 不变，
    `static_assert` 守护）；每个 TCB 追加固定 `PollWaitNode poll_nodes[POLL_MAX_WAIT_QUEUES]`
    数组，位于稳定的每线程存储，故登记与唤醒路径均不分配。
  - 在关中断的调度临界内为每个队列登记一个 poll 节点，再查谓词。若已满足则注销
    并直接返回，不阻塞。否则（对正超时）在既有睡眠链记录 deadline，阻塞并让出。
  - 恢复后（任一队列唤醒或超时），被唤醒线程在各队列锁下自摘除其登记过的 poll
    节点。
  - `wake_one`/`wake_all` 额外排空目标队列的 `poll_head`：对每个 poll 节点的
    owner 幂等置 runnable（复用 `wake_thread_locked`），并只从本队列摘除该节点。
    生产者仅持本队列锁，且分别获取各 owner 的域锁（绝不同时持两把锁）；跨队列清理
    留给被唤醒线程。由于 level-triggered，spurious/重复唤醒只多一次重扫。
- 等待返回后重扫描描述符集合以回填 `revents` 并重新计数。调度器私有的
  `WAIT_OK`/`WAIT_TIMEOUT` 结果不外泄：超时且无就绪只返回当前就绪个数（通常 0）。

单队列 `wait_queue_wait_until`/`wake_one`/`wake_all` 语义不变；从未有多队列等待者
的队列 `poll_head` 恒空，`drain_poll_head` 对其为空操作。

## 立即返回快路径

首次扫描发现有就绪、或 `timeout_ms == 0`、或上下文不可阻塞
（`!sched::can_block()`）时，`poll_fds_current` 立即返回（不进入任何等待队列），
与其它非阻塞短路一致。

## syscall 封装与用户 libc 镜像

- `sys_poll`（`kernel/core/syscall/syscall.cc`）校验 `nfds <= POLL_MAX_FDS`，用
  `validate_user_io_buffer` / `copy_current_user_buffer` 校验并拷入用户数组到定容
  内核工作副本，运行共享的 `proc::poll_fds_current` 核，再用
  `copy_to_current_user_buffer` 拷回。`nfds == 0` 跳过用户缓冲访问。
- `user/libc/include/poll.h` 定义匹配的 `struct pollfd`、`POLL*` 事件位、
  `POLL_MAX_FDS` 与 `int poll(struct pollfd *, unsigned long nfds, int timeout);`，
  并在头注释说明有界子集边界。`user/libc/syscall.c` 的封装透传 `SYS_POLL`
  （`rdi/rsi/rdx`），把负返回翻译为 `errno` 并返回 `-1`，保持 freestanding-safe。

## 验证

- 默认关闭的 xmake 开关 `--fd_multiplexing_smoke=y` 映射到
  `BIGOS_FD_MULTIPLEXING_SMOKE` 宏，遵循既有 smoke 选项模式。默认关闭，不改变
  默认启动行为。
- smoke 入口（`bigos::proc::fd_multiplexing_smoke_entry`，由
  `kernel/core/kernel.cc` spawn）在一个有界进程上下文（通过
  `set_current_user_process` 向调度器注册，使阻塞 poll 让出到 init/shell 后其进程
  槽在恢复时被还原）的可阻塞内核线程中运行，断言：`nfds > POLL_MAX_FDS` 返回
  `-EINVAL`；零超时是立即返回的非阻塞探测；正超时且无就绪时真正阻塞（以 monotonic
  tick 差佐证让出）并返回 0；坏 fd 项回填 `POLLNVAL` 并计入、负 fd 项被忽略，且都
  不使整调用失败；一个生产者线程在 poll 阻塞期间写入 pipe 唤醒等待者并只报告该项；
  以及“poll 报可读”蕴含非阻塞读不 would-block。它发出确定性
  `BIGOS_FD_MULTIPLEXING_PASSED` / `BIGOS_FD_MULTIPLEXING_FAILED` COM1 标记，
  通过 QEMU headless 路径验证。
- 内核/用户契约（`SYS_POLL = 61` 追加而不改号、`pollfd` 布局、`POLL*` 事件位、
  `POLL_MAX_FDS`）由源级契约测试 `tests/test_syscall_entry_source.py` 钉死。

## 非目标

- 不实现完整 POSIX `poll`/`select`/`epoll`/`ppoll`；不支持无界或动态增长的描述符
  集合（集合定容，超限确定性 `-EINVAL`）。
- 不实现边缘触发、事件通知对象（eventfd/epoll fd）、`POLLPRI`/带外数据或
  `POLLRDHUP`。
- 不实现超出底层有界阻塞原语行为的 `-EINTR` 信号中断 restart 语义。
- 多路复用不为常规文件或块设备引入新的就绪语义：它们在就绪模型下恒就绪，poll 立即
  返回。
- 不改动既有 syscall 编号、`int 0x80` ABI、boot/链接/向量/页表/磁盘布局，或单队列
  调度器等待/唤醒语义。
