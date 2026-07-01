# bounded-fd-multiplexing 验证记录

本变更在统一 fd 就绪模型与调度等待队列之上新增有界 `poll(2)` 风格的多路复用
syscall：追加 `SYS_POLL = 61` 与 `struct pollfd`/`POLL*` 事件位/`POLL_MAX_FDS`，
调度器追加多队列注册阻塞原语 `wait_queue_wait_any`（`WaitQueue.poll_head` +
TCB 固定 `PollWaitNode` 数组），VFS 追加只读 `poll_wait` op 收集各后端就绪等待
队列，`proc::poll_fds_current` 为内核共享核。新增默认关闭的 `fd_multiplexing_smoke`
开关与 `BIGOS_FD_MULTIPLEXING_SMOKE` 宏及 COM1 标记。不改动既有 syscall 编号、
`int 0x80` ABI、单队列调度语义或默认启动行为。

## 改动范围

调度器：

- `include/bigos/sched.h`：新增 `POLL_MAX_WAIT_QUEUES = 32` 与
  `wait_queue_wait_any` 声明；`WaitQueue` 末尾追加 `poll_head`（既有
  `head`/`tail`/`lock` 不变，`static_assert` 偏移守卫）。
- `kernel/core/sched/sched.cc`：`TCB` 追加 `PollWaitNode poll_nodes[POLL_MAX_WAIT_QUEUES]`
  与 `poll_registered`（创建/idle 初始化）；新增 `poll_queue_push_locked`/
  `poll_queue_remove_locked`/`drain_poll_head` 辅助；实现 `wait_queue_wait_any`
  （关中断 + 调度临界内先注册后查谓词、恢复自清理）；`wake_one`/`wake_all` 追加
  排空 `poll_head`；`init_wait_queue` 初始化 `poll_head`。

VFS 与后端：

- `include/bigos/fs/vfs.h`：前置声明 `sched::WaitQueue`；`FileOperations` 末尾追加
  `PollWaitOp poll_wait`（`static_assert` 守护为第 7 槽）；声明
  `file_poll_wait_queues`。
- `kernel/core/fs/vfs.cc`：实现 `file_poll_wait_queues`（空 op 返回 0）；`poll_file`
  不变。
- `kernel/core/ipc/pipe.cc`：`pipe_poll_wait`（读端 `read_wq`/写端 `write_wq`），
  写入 `PIPE_READ_OPS`/`PIPE_WRITE_OPS` 新槽位。
- `kernel/core/net/socket.cc`：`socket_poll_wait`（绑定活跃端点返回 `rx_wait`，
  否则 0 队列），写入 `SOCKET_OPS` 新槽位。
- `kernel/core/terminal/tty.cc`：`tty_poll_wait`（返回共享 `g_input_wait`），写入
  `TTY_OPS` 新槽位（保持单行初始化以满足既有源级契约）。

syscall 与进程核：

- `include/bigos/syscall.h`：追加 `SYS_POLL = 61`、`struct pollfd`、
  `POLLIN/POLLOUT/POLLERR/POLLHUP/POLLNVAL` 与 `POLL_MAX_FDS = 16`。
- `include/bigos/proc.h`：前置声明 `bigos::sys::pollfd`；声明 `poll_fds_current`；
  在 `BIGOS_FD_MULTIPLEXING_SMOKE` 下声明 `fd_multiplexing_smoke_entry`。
- `kernel/core/proc/proc.cc`：实现 `poll_fds_current`（扫描映射 `READY_*→POLL*`、
  坏 fd `POLLNVAL`、负 fd 忽略、立即返回快路径、收集去重等待队列、
  `wait_queue_wait_any` 阻塞、唤醒重扫）；新增 smoke 入口（有界进程上下文，经
  `set_current_user_process` 注册以在阻塞后恢复进程槽）。
- `kernel/core/syscall/syscall.cc`：`sys_poll`（校验 `nfds<=POLL_MAX_FDS`、拷入
  拷回用户数组、委派 `poll_fds_current`）；`dispatch` 追加 `case SYS_POLL`。

用户 libc：

- `user/libc/include/sys_nr.h`：追加 `SYS_POLL 61`。
- `user/libc/include/poll.h`（新建）：`struct pollfd`、`POLL*`、`POLL_MAX_FDS`、
  `poll()` 声明与有界子集注释。
- `user/libc/syscall.c`：`poll` wrapper 透传 `SYS_POLL` 并翻译 errno；包含 `poll.h`。

构建、测试与文档：

- `xmake/options.lua`：新增默认关闭 `fd_multiplexing_smoke` 选项。
- `xmake/kernel.lua`：映射到 `BIGOS_FD_MULTIPLEXING_SMOKE` 宏。
- `kernel/core/kernel.cc`：`#ifdef BIGOS_FD_MULTIPLEXING_SMOKE` spawn 入口线程。
- `tools/bigosdev/core.py`：`SMOKE_OPTIONS` 注册 `fd_multiplexing_smoke`。
- `tests/test_syscall_entry_source.py`：新增
  `test_bounded_fd_multiplexing_poll_contract_matches_across_kernel_and_user`。
- `docs/en/arch/bounded-fd-multiplexing.md` + `docs/zh/arch/bounded-fd-multiplexing.md`：
  新增同相对路径的双语说明。

## 已通过的检查

- `xmake`（x86_64-elf-gcc 默认交叉构建，`--fd_multiplexing_smoke=n`）：编译通过，
  默认启动路径不变。
- `xmake f --fd_multiplexing_smoke=y && xmake`：smoke 配置编译通过。
- QEMU headless smoke（smoke 开）：
  `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/fd-multiplexing-smoke.serial.log --expect-serial-marker BIGOS_FD_MULTIPLEXING_PASSED`
  → 观察到 `BIGOS_FD_MULTIPLEXING_PASSED`，无 `_FAILED`。smoke 覆盖：
  `nfds>POLL_MAX_FDS` → `-EINVAL`；零超时非阻塞探测立即返回；正超时无就绪真阻塞
  （monotonic tick 差佐证让出）返回 0；坏 fd `POLLNVAL` 计入、负 fd 忽略、不使整
  调用失败；生产者线程写 pipe 唤醒阻塞的 poll 且只报告该项；poll 报可读 ⇔ 非阻塞
  读不 would-block。
- QEMU headless 默认启动（smoke 关）：`--expect-serial-marker BIGOS_USER_EXEC`
  → 观察到 `BIGOS_USER_EXEC`，serial 日志无任何 `FD_MULTIPLEXING` 输出，默认进入
  userland 行为未变。
- clang 辅助静态检查（freestanding C++17、`-target x86_64-elf`、no hosted runtime、
  `-fno-exceptions`、`-fno-rtti`、项目 include 路径含 `cpp/include`、
  `cpp/libsupc++/include`）：`kernel/core/sched/sched.cc`、`kernel/core/fs/vfs.cc`、
  `kernel/core/ipc/pipe.cc`、`kernel/core/net/socket.cc`、
  `kernel/core/terminal/tty.cc`、`kernel/core/syscall/syscall.cc` 均 0 error、
  0 本次新增告警；`kernel/core/proc/proc.cc`（带
  `-DBIGOS_USER_PROCESS -DBIGOS_FD_MULTIPLEXING_SMOKE`）0 error。
- Python 校验：`uv run pytest tests/test_syscall_entry_source.py` → 20 passed
  （含新增 poll 契约测试）。全量 `uv run pytest` → `20 failed, 326 passed`；经
  `git stash` 移除本次改动后复跑为 `20 failed, 326 passed`，失败用例集合按 `comm`
  逐行 diff 完全一致（本次新增失败 0）。

## 调度器改动专项复核

- 中断安全：`wait_queue_wait_any` 在关中断的调度临界内完成登记/谓词检查/阻塞决策，
  复用与 `wait_queue_wait_until` 相同的“检查-入队原子化”模式，避免丢唤醒。
- 锁序：生产者（`drain_poll_head`）仅持本队列锁摘一个节点，释放后再单独取该 owner
  的域锁，绝不同时持两把锁（与 `wake_one` 既有纪律一致）；被唤醒线程在各队列锁下
  自清理，无跨队列锁序。
- 唤醒幂等与分配无关：`wake_thread_locked` 状态幂等，多队列唤醒同一线程只产生一次
  runnable；poll 节点为 TCB 内嵌稳定存储，登记/唤醒零分配。
- 单队列回归：`wait_queue_wait_until`/`sleep_for`/单队列 `wake_*` 路径零改动；从未
  有多队列等待者的队列 `poll_head` 恒空，`drain_poll_head` 对其为空操作。

## 因环境无法独立运行 / 残留风险

- clangd（7.4 范围内的辅助诊断）：其诊断与 `-fsyntax-only` 等价，已被上述 clang
  检查覆盖；未单独跑 clangd LSP 会话。权威检查仍是 GCC 交叉构建。
- 用户态端到端 `poll()`：`sys_poll` 需经 `int 0x80` 携带已验证用户缓冲区，无法从
  内核线程 smoke 直接驱动。smoke 改为在真实 fd 表上通过共享核 `poll_fds_current`
  验证全部语义（含真阻塞 + 生产者线程唤醒）；`sys_poll` 的用户缓冲校验/拷入拷回
  分支已由源码与构建覆盖。残留风险：跨进程用户态 `poll()` 的端到端返回需后续用户态
  smoke 或事件循环示例回归。
- socket/tty 就绪唤醒的真实 I/O 路径：smoke 用 pipe 覆盖真阻塞+唤醒；socket
  `rx_wait`、tty `g_input_wait` 的 `poll_wait` 句柄由源码与 fd-readiness 既有 smoke
  覆盖，但未在本 smoke 内驱动其真实数据到达唤醒。残留风险：socket 收包/终端键盘
  输入触发的 poll 唤醒需带网络/输入能力的 emulator 回归。

## 本次变更引入并已解决的问题

- `proc.h` 在 `poll_fds_current` 声明处直接使用 `bigos::sys::pollfd` 导致
  `'bigos::sys' has not been declared`（proc.h 不包含 syscall.h）。已在 proc.h
  追加 `namespace bigos::sys { struct pollfd; }` 前置声明，proc.cc 内含 syscall.h
  取完整类型，默认构建通过。
- smoke 入口作为纯内核线程（`user_process == nullptr`）在 poll 真阻塞让出到
  init/shell 后丢失 `current_process_slot`，唤醒后重扫得到 `POLLNVAL`（无 fd 表）。
  已改为在 smoke 进程上下文经 `sched::set_current_user_process(&proc)` 注册（并把
  `address_space_root`/`kernel_address_space_root` 置 `INVALID_PHYS_ADDR` 使恢复
  只重绑进程槽、不激活 CR3），退出时清除注册；QEMU smoke 转为 `_PASSED`。
- `tty.cc` 的 `TTY_OPS` 初始化最初拆成多行，触发既有源级契约测试
  `test_default_user_stdout_and_stderr_reach_visible_console` 断言（要求单行前缀
  `const vfs::FileOperations TTY_OPS = {&tty_read, &tty_close, &tty_write, &tty_lseek,`）
  失败。已改回单行初始化并追加 `&tty_poll_wait` 槽位，测试恢复通过、失败集合回到
  基线。
