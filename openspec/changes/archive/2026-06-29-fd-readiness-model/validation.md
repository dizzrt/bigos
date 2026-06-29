# fd-readiness-model 验证记录

本变更把分散的描述符就绪判断收敛为统一的内核内 `vfs::poll_file(File*)` 入口，
新增内核内就绪位常量与 `FileOperations.poll` 追加槽位，为 pipe、socket、tty 三类
后端实现 `poll` op，并为 UDP `UdpEndpoint` 补齐接收等待队列（RX 投递先入队再唤醒）。
新增默认关闭的 `fd_readiness_smoke` 开关与 `BIGOS_FD_READINESS_SMOKE` 宏及对应 COM1
标记。不新增 syscall 编号、不改 syscall ABI、不改默认启动行为。

## 改动范围

内核与头文件：

- `include/bigos/fs/vfs.h`：新增 `READY_READABLE`/`READY_WRITABLE`/`READY_ERROR`
  内核内就绪位常量；在 `FileOperations` 末尾追加 `PollOp poll` 槽位（既有
  `read/close/write/lseek/truncate/readdir` 槽位不重排），并以 `static_assert`
  偏移守卫锁定布局；声明 `vfs::poll_file(File*)`。
- `kernel/core/fs/vfs.cc`：实现 `poll_file`（`ops->poll` 非空时调用，否则返回
  确定性 `READY_READABLE|READY_WRITABLE`，空文件返回 `READY_ERROR`）；为既有
  `EXFAT_FILE_OPS`/`BIGFS_FILE_OPS` 显式补 `poll = nullptr`。
- `kernel/core/ipc/pipe.cc`：实现 `pipe_poll`，复用 `read_ready`/`write_ready`
  与 open 标志映射可读（含写端关闭 EOF）、可写、读端关闭时的写端 `READY_ERROR`；
  挂接到 `PIPE_READ_OPS`/`PIPE_WRITE_OPS`。
- `include/bigos/net.h`：`UdpEndpoint` 新增 `sched::WaitQueue rx_wait`，并引入
  `bigos/sched.h`。
- `kernel/core/net/protocol.cc`：`udp_bind` 处 `init_wait_queue(&rx_wait)`；
  `handle_udp` 入队 datagram 后 `wake_all(&rx_wait)`（先入队再唤醒）。
- `kernel/core/net/socket.cc`：实现 `socket_poll`（绑定且队列非空→可读，
  可发送→可写，未绑定/失活→错误位），挂接到 `SOCKET_OPS`。
- `kernel/core/terminal/tty.cc`：为 `TTY_OPS` 实现 `tty_poll`，复用
  `input_available` 作为可读位，写出恒可写，输入不置错误位，纯只读快照。
- `kernel/core/proc/proc.cc`：file-mapping smoke 的 `g_ops` 聚合初始化补
  `poll = nullptr`（消除追加槽位引入的 `-Wmissing-field-initializers`）。
- `kernel/core/kernel.cc`：新增 `BIGOS_FD_READINESS_SMOKE` 入口线程，断言
  pipe/socket/tty 三类描述符就绪与阻塞行为一致、poll 不出队、socket RX 唤醒、
  共享 `TTY_OPS` 的两个终端 fd 就绪一致，发 `BIGOS_FD_READINESS_PASSED`/`_FAILED`。
- `kernel/core/syscall/syscall.cc`：未改动，复核确认 `sys_recvfrom` 对外语义不变
  （`NoData → -EAGAIN`，等价 `-EWOULDBLOCK`）。

构建与文档：

- `xmake/options.lua`：新增默认关闭 `fd_readiness_smoke` 选项。
- `xmake/kernel.lua`：映射到 `BIGOS_FD_READINESS_SMOKE` 宏。
- `docs/en/arch/fd-readiness-model.md` + `docs/zh/arch/fd-readiness-model.md`：
  新增同相对路径的双语说明（统一入口、就绪位、各后端语义、socket 等待队列、
  tty 桥接、默认关闭 smoke、非目标）。

## 已通过的检查

- `xmake`（x86_64-elf-gcc 默认交叉构建，`--fd_readiness_smoke=n`）：编译通过，
  默认启动路径不变。
- `xmake f --fd_readiness_smoke=y && xmake`：smoke 配置编译通过。
- QEMU headless smoke：
  `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/fd_readiness_serial.log --expect-serial-marker BIGOS_FD_READINESS_PASSED`
  → 观察到 `BIGOS_FD_READINESS_PASSED`（serial 日志第 13 行）。
- clang 辅助静态检查（freestanding C++17、`--target=x86_64-elf`、`-mno-sse/-sse2/-mmx`、
  `-mno-red-zone`、`-fno-rtti`、`-fno-exceptions`、项目 include）：
  `kernel/core/fs/vfs.cc`、`kernel/core/ipc/pipe.cc`、`kernel/core/net/socket.cc`、
  `kernel/core/net/protocol.cc`、`kernel/core/terminal/tty.cc`、
  `kernel/core/kernel.cc`（带 `-DBIGOS_FD_READINESS_SMOKE`）均 0 error；
  `-Wall -Wextra` 下，vfs/pipe/socket/protocol/tty/kernel.cc 0 个本次新增告警。
- Python 校验：`uv run pyright` 0 errors/0 warnings；`uv run pytest` 全量
  `20 failed, 325 passed`，stash 对比确认变更前后失败集合与计数完全一致（本次 0 新增失败）；
  直接受影响的源码断言测试 `tests/test_tty_console_input_source.py`、
  `tests/test_writable_fs_page_cache_pipe_source.py`、`tests/test_fd_vfs_shell_source.py`
  共 47 例全部通过。

## 历史诊断（变更前已存在，非本次引入）

- `uv run pytest` 全量 20 个失败用例（如 `test_user_address_space_vmem_source`、
  `test_fork_copy_on_write_source`、`test_user_elf_program_loader_source` 等），
  经 `git stash` 移除本次改动后复跑，失败集合与计数完全一致，均为既有源码漂移，
  与本变更无关。
- `uv run ruff check` 报 4 处错误，全部落在本次未改动的
  `tests/test_tty_console_input_source.py:192/210/370/452`（历史长行/格式建议）；
  本变更未新增任何 Python 代码。
- clang `-Wall -Wextra` 对 `kernel/core/proc/proc.cc` 的 `-Wmissing-field-initializers`
  告警落在既有 VMA 聚合初始化（`file_backing` 字段）上，非本次新增代码。

## 因环境无法独立运行 / 残留风险

- clangd（6.4 范围内的辅助诊断）：其诊断与 `compile_commands.json` 驱动的 clang
  `-fsyntax-only` 等价，已被上述 clang 检查覆盖；未单独跑 clangd LSP 会话。权威
  检查仍是 GCC 交叉构建。
- 多路复用/事件循环端到端：本变更仅提供内核内就绪查询与 socket 等待队列，未引入
  用户可见 `poll`/`select` syscall，相应端到端事件循环行为属后续变更，未在本次验证。
- socket RX 唤醒的真实多线程阻塞-唤醒：smoke 在单一就绪线程内验证“注入后 poll 可读
  且 datagram 仍可收”的快照一致性与唤醒调用路径；未构造跨线程阻塞读者实际被
  `rx_wait` 唤醒的并发场景（`sys_recvfrom` 仍沿用既有 poll-and-yield，未改为基于
  等待队列阻塞）。残留风险：跨线程唤醒竞态需后续多路复用接入时再回归。

## 本次变更引入并已解决的问题

- `FileOperations` 追加 `poll` 槽位后，既有 `EXFAT_FILE_OPS`/`BIGFS_FILE_OPS` 与
  proc.cc file-mapping smoke 的 `g_ops` 聚合初始化触发 clang
  `-Wmissing-field-initializers`（本次新增有效告警）。已为这三处显式补
  `poll = nullptr` 解决；GCC 默认构建与 clang `-Wall -Wextra` 双通过。
- `TTY_OPS` 初始化曾被改为多行格式，破坏 `tests/test_tty_console_input_source.py`
  对单行前缀的断言。已恢复为单行并追加 `&tty_poll`，测试通过。
