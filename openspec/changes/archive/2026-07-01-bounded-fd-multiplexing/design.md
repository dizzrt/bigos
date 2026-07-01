## Context

内核已具备本变更所需的两块基础：

- 统一 fd 就绪模型（归档 change `fd-readiness-model`）：`vfs::poll_file(File*)` 经 `FileOperations.poll` op 派发，pipe/socket/tty 各自实现的 `poll` op 复用与阻塞读写同源的谓词，返回 `READY_READABLE`/`READY_WRITABLE`/`READY_ERROR` 位标志（`include/bigos/fs/vfs.h`）。这是纯只读快照，不出队、不阻塞。
- 有界非阻塞描述符行为（归档 change `nonblocking-fd-io`）：`vfs::File.nonblocking` + `SYS_FCNTL` 的 `F_GETFL`/`F_SETFL`，读写在“将要阻塞”判定点确定性返回 would-block。

调度器（`kernel/core/sched/sched.cc`）提供 `wait_queue_wait_until(queue, predicate, arg, timeout_ticks)`、`wake_one`/`wake_all` 与基于 monotonic tick 的超时睡眠。关键约束：**TCB 目前只支持“至多属于一个 wait queue”**——`TCB` 只有单一 `wait_next` 链指针与 `wait_queue` 归属指针（`kernel/core/sched/sched.cc` 注释明确“A thread may belong to at most one explicit wait queue and one timeout tracking list at a time”）。因此“一个线程同时等待多个描述符就绪”不能直接用现有单队列原语表达，需要对调度器做一次追加式扩展。

各后端的就绪等待队列已就位且已在数据到达/状态变化时 `wake_*`：pipe 的 `read_wq`/`write_wq`（`kernel/core/ipc/pipe.cc`），UDP socket 的 `rx_wait`（`include/bigos/net.h`、`kernel/core/net`），tty 的全局输入等待队列（`kernel/core/terminal/tty.cc`）。

syscall surface（`include/bigos/syscall.h`）当前最大编号为 `SYS_DYN_PROTECT = 60`，寄存器传参与返回约定固定、由源级检查钉死。用户指针经 `bigos::proc::validate_user_io_buffer`/`copy_*_user_buffer` 校验。

本变更是 M13 深度阶段第三步：在统一就绪模型与非阻塞行为之上，提供用户可见的有界多路复用 syscall，让单线程程序一次等待多个描述符并编写事件循环。

## Goals / Non-Goals

**Goals:**

- 提供一个用户可见、有界的多路复用 syscall（poll(2) 风格）：接受定容 pollfd 数组与毫秒超时，阻塞等待直到集合中至少一个描述符就绪、超时到期、或确定性错误，返回就绪描述符个数（超时返回 0）。
- 就绪判定完全复用 `vfs::poll_file`（level-triggered），事件位与内核内 `READY_*` 同源，避免各自维护、可能漂移的判定。
- 复用调度等待队列实现真正阻塞让出：以追加式扩展让一个线程在同一次阻塞中登记到多个后端就绪等待队列上，任一 `wake_*` 或超时唤醒后重扫就绪集合；登记/注销/唤醒均分配无关、IRQ-safe。
- ABI append-only：仅新增 `SYS_POLL = 61`，不动既有编号、寄存器顺序、`int 0x80` 返回与 no-EOI 规则。
- 内核与用户两份 pollfd 布局/事件位一致；新增默认关闭 smoke 验证。

**Non-Goals:**

- 不实现完整 POSIX `poll`/`select`/`epoll`/`ppoll`；不支持无界或动态增长描述符集合（集合定容，超出确定性拒绝）。
- 不实现边缘触发、事件通知对象（eventfd/epoll fd）、`POLLPRI`/带外数据、`POLLRDHUP` 等扩展事件。
- 不实现被信号中断后的 `-EINTR` 复杂 restart 语义；信号交互保持既有阻塞原语的既定行为。
- 不把多路复用引入常规文件/块设备的“恒就绪”之外的任何新语义（它们经就绪模型默认恒可读可写，poll 立即返回）。
- 不改动既有 syscall 编号、`int 0x80` ABI、boot/链接脚本/IDT/syscall 向量、页表布局或磁盘布局。
- 不改变既有单队列 `wait_queue_wait_until`/`wake_one`/`wake_all` 的对外语义。

## Decisions

### 决策一：追加 `SYS_POLL = 61`，poll(2) 风格 pollfd 数组 + 毫秒超时

在 `include/bigos/syscall.h` 末尾追加 `SYS_POLL = 61`（当前最大号 60）。寄存器约定沿用既有 ABI：`rdi=用户 pollfd 数组指针`，`rsi=nfds`，`rdx=timeout_ms`；返回值写回 `rax`（就绪 fd 个数，超时 0，或负 errno）。

用户可见结构（内核 `bigos/syscall.h` 与 `user/libc` 两份保持一致）：

```
struct pollfd {
    int32_t  fd;       // 被监听描述符；负值表示忽略该项（revents 置 0）
    uint16_t events;   // 请求关注的事件位（POLLIN/POLLOUT 子集）
    uint16_t revents;  // 内核回填的就绪事件位（含 POLLERR/POLLNVAL/POLLHUP）
};
```

事件位（有界子集，取值与 Linux 常见值对齐，便于移植）：`POLLIN = 0x001`、`POLLOUT = 0x004`、`POLLERR = 0x008`、`POLLHUP = 0x010`、`POLLNVAL = 0x020`。`POLLERR`/`POLLHUP`/`POLLNVAL` 是“输出专用”——即便 `events` 未请求也会在对应条件下回填。

- `nfds` 上限为定容 `POLL_MAX_FDS`（建议 16，与历史 fd 表基线量级一致）。`nfds > POLL_MAX_FDS` 确定性返回 `-EINVAL`；`nfds == 0` 合法（退化为纯超时睡眠，返回 0）。
- `timeout_ms` 语义：`> 0` 带超时阻塞；`== 0` 只做一次就绪扫描立即返回（非阻塞探测）；`< 0` 无限等待（无超时）。毫秒→tick 复用 `SYS_SLEEP_MS` 使用的粗粒度 monotonic tick 换算。
- 备选：新增 `SYS_SELECT` 或复用某既有 syscall。否决：select 的 fd_set 位图对定容小集合更笨重且需三份掩码往返；poll 的数组式 pollfd 更直接、单次拷入拷出。复用既有 syscall 无自然宿主（不同于 `O_NONBLOCK` 可挂 `SYS_FCNTL`），故按 `SYS_SOCKET..SYS_DYN_PROTECT` 的先例追加新号。

### 决策二：就绪判定复用 `vfs::poll_file`（level-triggered），事件位与 `READY_*` 同源

对每个 `fd >= 0` 的 pollfd，用 `bigos::proc::file_for_fd_current(fd)` 取 `vfs::File*`：

- fd 非法/未打开：`revents |= POLLNVAL`，计入就绪个数（与 poll(2) 一致——坏 fd 不使整个调用失败，而是逐项标记）。
- 有效 fd：调用 `vfs::poll_file(file)` 取 `READY_*` 位，按下表映射并与 `events` 求交后回填 `revents`：
  - `READY_READABLE` → `POLLIN`（仅当 `events & POLLIN`）
  - `READY_WRITABLE` → `POLLOUT`（仅当 `events & POLLOUT`）
  - `READY_ERROR` → `POLLERR`（无条件回填）
  - pipe 读端在写端关闭且缓冲空的“可读 EOF”场景由 `poll_file` 报 `READY_READABLE`；写端在读端关闭报 `READY_ERROR`，本层附带回填 `POLLHUP` 以表达对端关闭（有界表达，不引入独立 hangup 检测路径）。

某 pollfd 的 `revents != 0` 即视为“该项就绪”，计入返回个数。level-triggered：只要条件持续满足就持续报告，语义与非阻塞读写、阻塞谓词三者同源（`poll_file` 报可读 ⇔ 非阻塞读不 would-block），这是本变更正确驱动非阻塞 fd 的前提，由 smoke 交叉验证。

### 决策三：调度器追加“多队列注册阻塞”原语（复用等待队列，分配无关、IRQ-safe）

核心难点是让一个线程同时等待多个后端就绪队列，而 TCB 现只支持单队列归属。方案（全部追加式，不改既有单队列路径语义）：

1. **TCB 追加固定容量 poll 节点数组**：`PollWaitNode poll_nodes[POLL_MAX_WAIT_QUEUES]`（建议 32，覆盖每 fd 最多贡献读+写两个队列后再去重的上界）。节点 `{ WaitQueue *queue; TCB *owner; PollWaitNode *q_next; }`，随 TCB 静态存续、永不在等待/唤醒路径分配，满足“唤醒分配无关”契约。
2. **`WaitQueue` 末尾追加一个 `poll_head` 指针**（poll 节点单链表头），既有 `head`/`tail`/`lock` 布局与语义不变，以 static_assert 守护偏移。单等待路径（`wait_next`/`wait_queue`）完全不受影响。
3. **新增原语 `int wait_queue_wait_any(WaitQueue **queues, uint32_t count, WaitPredicate predicate, void *arg, timeout_ticks)`**：
   - 关中断 + 进入调度临界 + 取相关队列锁后先查 `predicate`；为真则不阻塞直接返回 `WAIT_OK`。
   - 否则把当前 TCB 的前 `count` 个 poll 节点分别挂到各队列的 `poll_head`；`timeout>0` 记 deadline 并入睡眠链、状态 `Sleeping`，否则状态 `Blocked`。
   - `schedule` 让出。
   - 恢复后（被任一队列唤醒或超时）：遍历本 TCB 已注册的 poll 节点，各取对应队列锁把自身从 `poll_head` 摘除（自清理），返回 `wait_result`。
4. **唤醒侧扩展**：`wake_one`/`wake_all`（生产者，可能在 IRQ/投递上下文）除处理既有单等待 TCB 链外，同时**排空 `poll_head`**：对每个 poll 节点取 `owner` TCB、幂等地置 runnable（复用 `wake_thread_locked` 的状态幂等），并把该节点从**本队列**摘除；**不跨队列摘节点**（跨队列清理留给被唤醒线程在恢复路径自做，避免跨队列锁序问题）。
   - 竞态安全：poll 节点存于 TCB 稳定存储、TCB 不在此路径释放；生产者与被唤醒线程对“同一队列的同一节点”的摘除都在该队列锁下串行；`wake_thread_locked` 幂等，重复/spurious 唤醒只是让 poll 线程多重扫一次 `poll_file`，因 level-triggered 而安全（不就绪则重注册再阻塞，syscall 层有界重试）。

- 备选 A：给 TCB 加“多重 `wait_next` 数组”并让 `WaitQueue` 直接串 TCB。否决：`WaitQueue` 需容纳异构/多重成员，破坏既有单链不变式，且跨队列摘除需复杂锁序。
- 备选 B：不改调度器，poll syscall 用 `sched::sleep_for` 小片轮询 + 每片重扫 `poll_file`（即被否的“有界 deadline 轮询”方案）。否决：与 roadmap“复用调度等待队列”的要求不符，且在无就绪时仍周期性醒来空转，不是真正的事件驱动阻塞。
- 选择理由：追加式扩展既有等待队列，登记/唤醒分配无关、IRQ-safe，生产者只需本队列锁；被唤醒线程自清理规避跨队列锁序；spurious 唤醒因 level-triggered 而安全。

### 决策四：VFS 追加只读 `poll_wait` op 收集后端就绪等待队列

poll syscall 需要把“被监听 fd”映射到“要挂哪些等待队列”，但不应硬编码后端类型。在 `vfs::FileOperations` 末尾**追加**一个可选 op（不重排既有槽位，沿用 readiness `poll` op 的追加先例）：

```
using PollWaitOp = uint32_t (*)(File *file, uint32_t events, sched::WaitQueue **out, uint32_t max) noexcept;
```

返回写入 `out` 的队列个数（0..max）。各后端实现：pipe 按读/写端返回 `read_wq` 或 `write_wq`；socket 返回 `rx_wait`；tty 返回全局输入等待队列。未实现该 op 的后端（常规文件等）贡献 0 个队列——它们经 `poll_file` 默认恒就绪，poll 会立即返回，无需等待。`vfs.h` 仅需前置声明 `sched::WaitQueue`，不引入循环依赖。

poll syscall 汇总所有 fd 的队列后**去重**（有界线性扫描，上界 `POLL_MAX_WAIT_QUEUES`），再交给 `wait_queue_wait_any`。

### 决策五：poll 谓词与整体控制流

`wait_queue_wait_any` 的 `predicate` 就是“重扫所有被监听 fd，任一 `revents != 0`”。为在 IRQ-disabled 的谓词检查点安全执行，谓词只读调用 `poll_file`（纯快照、无分配、无阻塞），并把结果写入调用者提供的 pollfd 工作副本（内核栈上的定容数组，最后一次性拷回用户）。

控制流 / 数据流（同步、进程/ syscall 线程上下文）：

```
user poll(fds, nfds, timeout_ms)
  -> int 0x80 (SYS_POLL) -> sys_poll
     1. 校验 nfds<=POLL_MAX_FDS、用户数组可读写；拷入 pollfd 到内核工作副本
     2. 首次扫描：对每项 file_for_fd_current + poll_file，回填 revents，统计 ready
        - ready>0，或 timeout_ms==0，或 !can_block() -> 拷回，返回 ready
     3. 收集各有效 fd 的等待队列（ops->poll_wait），去重成 queues[count]
     4. wait_queue_wait_any(queues, count, poll_predicate, &work, ms_to_ticks(timeout))
        - predicate: 重扫 poll_file，任一就绪则真
        - 被 wake_* 或超时唤醒 -> 恢复后自清理 poll 节点
     5. 唤醒后再扫一次：回填 revents、统计 ready；拷回用户；返回 ready(超时且无就绪则 0)
```

唤醒侧（生产者，保持既有 `wake_*` 调用点不变）：pipe 写/读/关闭、tty 输入入队、socket RX 投递仍 `wake_one`/`wake_all`；本变更让这些唤醒额外排空对应队列的 `poll_head`，从而唤醒 poll 等待者。

### 决策六：失败与边界行为

- `nfds > POLL_MAX_FDS`：`-EINVAL`，不阻塞、不部分处理。
- 用户 pollfd 数组指针非法/长度越界：`-EFAULT`，不阻塞。
- 单个 fd 非法/未打开：该项 `revents = POLLNVAL` 并计入就绪个数（不使整调用失败），符合 poll(2)。
- `timeout_ms == 0`：只做一次扫描立即返回就绪个数（含 0），非阻塞探测。
- `timeout_ms < 0`：无限等待（`wait_queue_wait_any` 传 `timeout_ticks = 0` 表无超时）。
- 不可阻塞上下文（`!can_block()`，如从不可阻塞路径进入）且无就绪：退化为“单次扫描立即返回”，不入等待队列（与非阻塞短路一致）。
- 登记/唤醒分配无关：poll 节点为 TCB 固定存储；`poll_wait` op 只读；`poll_file` 只读。低层分配/IO/中断路径不被触及。
- 超时唤醒：复用既有睡眠链 + monotonic tick 到期唤醒（`WAIT_TIMEOUT`），syscall 层把 `WAIT_TIMEOUT` 翻译为“无新就绪 → 返回本次扫描就绪个数（通常 0）”，不向用户泄漏调度器私有常量。

## Risks / Trade-offs

- [多队列注册破坏既有单队列不变式或引入跨队列死锁] → 只追加 `WaitQueue.poll_head` 与 TCB 固定 poll 节点，单等待路径零改动；生产者仅持本队列锁摘本队列节点，跨队列清理由被唤醒线程在恢复路径自做，无跨队列锁序。
- [spurious/重复唤醒导致错误就绪] → level-triggered：唤醒后必重扫 `poll_file`，不就绪则重注册再阻塞；`wake_thread_locked` 幂等确保恰好一次 runnable。spurious 只增一次重扫成本，语义正确。
- [就绪谓词与非阻塞/阻塞判定漂移] → 三者强制复用同一 `poll_file`/后端 `poll` op；smoke 交叉断言“poll 报可读 ⇔ 非阻塞读不 would-block”。
- [错过唤醒（数据在 predicate 检查与入队之间到达）] → `wait_queue_wait_any` 在 IRQ-disabled + 队列锁下先查 predicate 再入队，复用 `wait_queue_wait_until` 已验证的“检查-入队原子化”模式，避免丢唤醒。
- [`FileOperations`/`WaitQueue`/`TCB` 追加字段影响布局] → 全部末尾追加并以 static_assert 守护既有偏移；既有后端不提供 `poll_wait` op 时贡献 0 队列、行为不变。
- [定容集合过小限制事件循环] → `POLL_MAX_FDS`/`POLL_MAX_WAIT_QUEUES` 为文档化有界常量，超限确定性 `-EINVAL`；本阶段目标是有界事件循环，不追求无界 fd 集合。
- [新增 syscall 号与“严禁新增编号”约束张力] → poll/select 无可自然复用的既有 syscall；采用与既有 `SYS_*` 追加一致的 append-only 做法，不动既有号与 ABI，并在 proposal/spec 显式记录该取舍。
- [内核/用户两份 pollfd 与事件位不一致] → 两份定义 + 源级契约校验（pytest）确保布局、字段偏移、事件位取值一致且与既有常量不冲突。
- [默认启动行为变化] → SYS_POLL 仅在用户显式调用时生效；smoke 默认关闭；默认 boot 进 shell 行为不变。

## Migration Plan

1. 调度器：`include/bigos/sched.h` 声明 `wait_queue_wait_any` 与 `POLL_MAX_WAIT_QUEUES`；`WaitQueue` 追加 `poll_head`。`kernel/core/sched/sched.cc` 给 TCB 追加固定 poll 节点数组，实现多队列登记/自清理与 `wake_one`/`wake_all` 排空 `poll_head`，加布局守护。
2. VFS：`include/bigos/fs/vfs.h` 追加 `PollWaitOp poll_wait` 到 `FileOperations` 末尾（前置声明 `sched::WaitQueue`），加 static_assert 守护槽位；`poll_file` 不变。
3. 各后端 `poll_wait` op：pipe（`kernel/core/ipc/pipe.cc`，读端→`read_wq`、写端→`write_wq`）、socket（`kernel/core/net`，→`rx_wait`）、tty（`kernel/core/terminal/tty.cc`，→输入等待队列）。
4. syscall：`include/bigos/syscall.h` 追加 `SYS_POLL = 61`、`struct pollfd`、`POLL*` 事件位与 `POLL_MAX_FDS`；`kernel/core/syscall/syscall.cc` 实现 `sys_poll`（拷入、扫描、收集去重队列、`wait_queue_wait_any`、回填拷回）并接入 dispatch。
5. 用户 libc：`user/libc/include/poll.h`（或既有头）补 `struct pollfd`、`POLL*`、`poll` 声明；`user/libc/syscall.c` 增 `poll` wrapper 透传 `SYS_POLL` 并翻译 errno；保持 freestanding-safe。
6. 验证：新增默认关闭 xmake 开关 → `BIGOS_*` 宏 → `kernel.cc` smoke 入口线程，覆盖“无就绪带超时阻塞后返回 0 / 某 fd 就绪后唤醒且只报告该 fd / 就绪与非阻塞读一致 / 坏 fd POLLNVAL / nfds 超限 EINVAL”，发 COM1 `BIGOS_*_PASSED`/`_FAILED`；更新源级 syscall/pollfd/事件位一致性校验；QEMU headless 验证。
7. 文档：`docs/en` 增补多路复用 syscall 说明并在 `docs/zh` 同步对应相对路径镜像。

回滚策略：本变更为追加式、默认关闭、不改默认启动与既有 ABI 编号。回滚时移除 `SYS_POLL` 分发、`sys_poll`、`wait_queue_wait_any`、`WaitQueue.poll_head`、TCB poll 节点与各后端 `poll_wait` op 即可，既有单队列阻塞、`poll_file` 只读查询与非阻塞读写语义不受影响。

## Open Questions

无未决问题。以下取舍已在 Decisions 中定稿：追加 `SYS_POLL = 61`（非复用既有号）、采用调度器多队列注册阻塞（非 deadline 轮询）、坏 fd 逐项 `POLLNVAL` 而非整调用失败、`timeout_ms < 0` 表无限等待。
