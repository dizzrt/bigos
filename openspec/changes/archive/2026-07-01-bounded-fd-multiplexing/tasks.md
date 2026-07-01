# bounded-fd-multiplexing 任务清单

按子系统拆分，依赖顺序自上而下：先做调度器多队列等待原语与 VFS `poll_wait` op 基础，再落地 syscall、用户 libc 镜像、smoke 与验证。涉及 `include`、`kernel/core`、`user/libc` 下的 C/C++ 改动，按规则附 clang/clangd 辅助静态检查与 QEMU headless smoke 验证；涉及内核/用户源级契约的部分附 `uv run pytest`。所有新增字段一律末尾追加并以 `_static_assert`/`static_assert` 守护既有布局。

## 1. 调度器多队列等待原语

- [x] 1.1 在 `include/bigos/sched.h` 声明 `constexpr uint32_t POLL_MAX_WAIT_QUEUES`（建议 32）与新原语 `int wait_queue_wait_any(WaitQueue **__queues, uint32_t __count, WaitPredicate __predicate, void *__arg, timer::tick_t __timeout_ticks = 0) noexcept`，语义为：先查 predicate，为真立即返回 `WAIT_OK`；否则把当前线程登记到 `__queues` 前 `__count` 个队列，被任一队列 `wake_*` 或超时唤醒后返回 `WAIT_OK`/`WAIT_TIMEOUT`。
- [x] 1.2 在 `include/bigos/sched.h` 的 `WaitQueue` 末尾追加一个 poll 节点单链表头（如 `void *poll_head`），不重排既有 `head`/`tail`/`lock`，以 `static_assert` 守护既有字段偏移不变。
- [x] 1.3 在 `kernel/core/sched/sched.cc` 给 `TCB` 追加固定容量 poll 节点数组（`PollWaitNode poll_nodes[POLL_MAX_WAIT_QUEUES]`，节点含 `WaitQueue *queue`、`TCB *owner`、链指针），随 TCB 静态存续，等待/唤醒路径不分配。
- [x] 1.4 在 `kernel/core/sched/sched.cc` 实现 `wait_queue_wait_any`：关中断 + 进入调度临界 + 取相关队列锁后先查 predicate；未就绪则把 poll 节点分别挂入各队列 `poll_head`，`timeout>0` 记 deadline 并入睡眠链（`Sleeping`）否则 `Blocked`，让出；恢复后遍历本线程 poll 节点在各队列锁下自摘除，返回 `wait_result`。复用既有“检查-入队原子化”模式避免丢唤醒。
- [x] 1.5 扩展 `wake_one`/`wake_all`：除处理既有单等待 TCB 链外，同时排空目标队列的 `poll_head`——对每个 poll 节点取 `owner` 幂等置 runnable（复用 `wake_thread_locked` 状态幂等）并把该节点从**本队列**摘除；不跨队列摘节点（跨队列清理留给被唤醒线程恢复路径）。
- [x] 1.6 复核多队列登记/唤醒的锁序与竞态：生产者仅持本队列锁；被唤醒线程自清理各队列；spurious/重复唤醒因 level-triggered 只导致一次重扫，`wake_thread_locked` 幂等保证恰好一次 runnable。确认既有单队列 `wait_queue_wait_until` 路径零改动、语义不变。

## 2. VFS poll_wait op 基础

- [x] 2.1 在 `include/bigos/fs/vfs.h` 前置声明 `namespace bigos::sched { struct WaitQueue; }`，在 `FileOperations` 末尾追加可选 op `using PollWaitOp = uint32_t (*)(File *__file, uint32_t __events, sched::WaitQueue **__out, uint32_t __max) noexcept;`（返回写入 `__out` 的队列数 0..max），不重排既有槽位，以 `static_assert` 守护 `poll_wait` 为最后一个槽位、`poll` 等既有槽位偏移不变。
- [x] 2.2 提供内核内辅助（如 `uint32_t file_poll_wait_queues(File*, uint32_t events, sched::WaitQueue **out, uint32_t max)`）：`ops->poll_wait` 为空的后端返回 0（常规文件等恒就绪，无需等待）。`poll_file` 保持不变，只读语义不动。

## 3. 各后端 poll_wait op

- [x] 3.1 在 `kernel/core/ipc/pipe.cc` 实现 `pipe_poll_wait`：读端返回 `&pipe->read_wq`、写端返回 `&pipe->write_wq`（按请求事件与端方向），写入 `PIPE_READ_OPS`/`PIPE_WRITE_OPS` 的新 op 槽位；复用与 `pipe_poll` 同源的端方向判断。
- [x] 3.2 在 `kernel/core/net`（`socket.cc` 及所属 endpoint）实现 socket `poll_wait`：返回 `&endpoint->rx_wait`；未绑定/失活端点返回 0 队列（其 `poll_file` 已报 `READY_ERROR`，poll 会立即就绪返回），写入 `SOCKET_OPS` 新槽位。
- [x] 3.3 在 `kernel/core/terminal/tty.cc` 实现 `tty_poll_wait`：返回终端输入等待队列句柄，写入 `TTY_OPS` 新槽位；确认所有终端 fd 共享同一 `TTY_OPS`，poll_wait 结果一致，不引入裸 fd 特例。
- [x] 3.4 复核：各后端 `poll_wait` 与 `poll` op 的就绪判定同源，`poll_wait` 只返回队列句柄、不改任何后端状态、不出队。

## 4. SYS_POLL syscall 与 ABI

- [x] 4.1 在 `include/bigos/syscall.h` 末尾追加 `SYS_POLL = 61`（当前最大 `SYS_DYN_PROTECT = 60`），保持寄存器约定：`rdi=pollfd*`、`rsi=nfds`、`rdx=timeout_ms`，返回就绪个数/0/负 errno。
- [x] 4.2 在 `include/bigos/syscall.h` 定义 `struct pollfd { int32_t fd; uint16_t events; uint16_t revents; }`、事件位常量 `POLLIN=0x001`/`POLLOUT=0x004`/`POLLERR=0x008`/`POLLHUP=0x010`/`POLLNVAL=0x020` 与定容上限 `POLL_MAX_FDS`（建议 16），并注明与 `user/libc` 镜像一致、与既有常量不冲突。
- [x] 4.3 在 `kernel/core/syscall/syscall.cc` 实现 `sys_poll`：校验 `nfds<=POLL_MAX_FDS`（否则 `-EINVAL`）、用户数组可读写并拷入内核工作副本（`validate_user_io_buffer`/`copy_current_user_buffer`）；`nfds==0` 合法。
- [x] 4.4 首次扫描：对每项 `fd<0` 忽略（`revents=0`）；无效 fd 回填 `POLLNVAL` 计入就绪；有效 fd 经 `file_for_fd_current` + `vfs::poll_file` 取 `READY_*`，按 `READY_READABLE→POLLIN`（与 `events&POLLIN` 求交）、`READY_WRITABLE→POLLOUT`（求交）、`READY_ERROR→POLLERR`（无条件）、对端关闭附带 `POLLHUP` 回填 `revents`，统计 `ready`。
- [x] 4.5 立即返回分支：`ready>0`、或 `timeout_ms==0`、或 `!can_block()` 时拷回用户数组并返回 `ready`，不进入等待队列。
- [x] 4.6 阻塞分支：收集各有效 fd 的等待队列（`file_poll_wait_queues`）并有界去重成 `queues[count]`（上界 `POLL_MAX_WAIT_QUEUES`）；调用 `wait_queue_wait_any(queues, count, poll_predicate, &work, ms_to_ticks(timeout_ms))`，其中 `timeout_ms<0` 传 `timeout_ticks=0`（无限等待）；`poll_predicate` 只读重扫 `poll_file` 判定任一就绪。
- [x] 4.7 唤醒后重扫：再次回填 `revents`、统计 `ready`；把工作副本拷回用户数组；返回 `ready`（超时且无就绪则 0）。`WAIT_TIMEOUT` 翻译为返回本次 `ready`，不泄漏调度器私有常量。
- [x] 4.8 在 `dispatch` 增加 `case SYS_POLL`，按 `BIGOS_USER_PROCESS` 守卫与既有 fd/VFS syscall 一致（多路复用属用户进程消费面）。

## 5. 用户 libc 镜像

- [x] 5.1 在 `user/libc/include/poll.h`（新建或既有位置）定义与内核一致的 `struct pollfd`、`POLLIN`/`POLLOUT`/`POLLERR`/`POLLHUP`/`POLLNVAL` 常量与 `int poll(struct pollfd *, unsigned long nfds, int timeout);` 声明；头注释说明有界子集边界。
- [x] 5.2 在 `user/libc/syscall.c` 增 `poll` wrapper：透传 `SYS_POLL`（`rdi/rsi/rdx`），返回值 `<0` 翻译为 errno 并返回 -1，`>=0` 直接返回；保持 freestanding-safe，不依赖 hosted 运行时。

## 6. 运行期 smoke 验证

- [x] 6.1 在 `xmake/options.lua` 新增默认关闭开关（如 `fd_multiplexing_smoke`），在 `xmake/kernel.lua` 映射到 `BIGOS_FD_MULTIPLEXING_SMOKE` 宏，遵循既有 smoke 选项模式。
- [x] 6.2 在 `kernel/core/kernel.cc` 的 smoke 阶段新增 `#ifdef BIGOS_FD_MULTIPLEXING_SMOKE` 入口线程，断言：无就绪集合带正超时后 `poll` 返回 0（真阻塞非忙等，以耗时/tick 佐证让出）；对某 pipe/socket/tty 使其就绪后 `poll` 唤醒并只在该项回填就绪位；`poll` 报可读 ⇔ 对该 fd 非阻塞读不 would-block；坏 fd 项回填 `POLLNVAL` 且不使整调用失败；`nfds>POLL_MAX_FDS` 返回 `-EINVAL`；`timeout==0` 非阻塞探测立即返回。发 COM1 `BIGOS_FD_MULTIPLEXING_PASSED`/`_FAILED` 标记。
- [x] 6.3 在 smoke 线程 bring-up 处按既有模式以 `#ifdef` 守卫 spawn 该入口线程；确认默认（开关关闭）构建不含该线程、默认启动行为不变。

## 7. 构建与静态检查

- [x] 7.1 运行 `xmake`（x86_64-elf-gcc 交叉构建）确认默认配置编译通过且默认启动行为不变；若交叉工具链不可用则显式记录该阻塞与残留风险。
- [x] 7.2 运行 `xmake f --fd_multiplexing_smoke=y && xmake` 确认 smoke 配置编译通过。
- [x] 7.3 对新增/修改的 C/C++ 文件执行 clang 辅助静态检查，配置尽量贴近 GCC 交叉环境（freestanding C++17、x86_64 target、项目 include 路径、no hosted runtime、no exceptions、no RTTI）；修复本次变更引入的 clang 错误，确认或修复有效新增告警。若等价 clang 标志不可用则记录差距与残留风险。
- [x] 7.4 对新增/修改的 C/C++ 文件执行 clangd 辅助诊断；区分历史诊断、本次变更诊断与 freestanding/工具链误报，修复本次引入的问题。
- [x] 7.5 调度器改动专项复核：多队列登记/唤醒的中断安全、可重入性、锁序（生产者仅本队列锁、被唤醒线程自清理）、唤醒幂等与分配无关，以及既有单队列/超时/睡眠链路径的回归可见性。

## 8. 源级契约与仿真器 smoke

- [x] 8.1 更新/扩展 `tests/test_syscall_entry_source.py`（或相邻源契约测试）断言：内核追加 `SYS_POLL=61` 且不改动既有编号；内核与 `user/libc` 的 `struct pollfd` 布局（字段顺序/大小/偏移）与 `POLL*` 事件位、`POLL_MAX_FDS` 取值一致且与既有 open flag/fcntl/syscall 常量不冲突。运行 `uv run pytest tests/test_syscall_entry_source.py`，`uv` 不可用时显式记录该阻塞。
- [x] 8.2 通过 QEMU headless 路径运行 smoke（启用 `fd_multiplexing_smoke`）并期待 COM1 `BIGOS_FD_MULTIPLEXING_PASSED`，例如 `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial.log --expect-serial-marker BIGOS_FD_MULTIPLEXING_PASSED`；若 QEMU/工具链/镜像不可用则显式记录跳过原因与残留风险。

## 9. 文档与验证记录

- [x] 9.1 在 `docs/en` 更新多路复用 syscall 说明（`SYS_POLL` ABI、`struct pollfd`/事件位、复用 `poll_file` 的 level-triggered 就绪映射、调度器多队列注册阻塞、超时/坏 fd/定容语义、与非阻塞读写与就绪模型一致性、默认关闭 smoke），并在 `docs/zh` 同步对应相对路径镜像；使用仓库相对路径，不暗示 boot/链接/向量/页表/磁盘/既有 ABI 编号变更。
- [x] 9.2 整理验证记录，分别列出已通过的检查、因环境无法运行的检查（含原因与残留风险）、历史诊断与本次变更引入的问题。
