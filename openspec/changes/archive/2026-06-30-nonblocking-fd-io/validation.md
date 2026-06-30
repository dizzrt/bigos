# nonblocking-fd-io 验证记录

本变更在统一 fd 就绪模型之上新增有界 `O_NONBLOCK` 子集：在 `vfs::File`（打开文件
描述粒度）末尾追加 `nonblocking` 标志，复用既有 `SYS_FCNTL` 路径新增 `F_GETFL`/
`F_SETFL` 命令，并在 pipe/tty 的“将要阻塞”判定点与 socket `sys_recvfrom` 的有界
RX 推进处接入非阻塞短路。新增默认关闭的 `nonblocking_fd_smoke` 开关与
`BIGOS_NONBLOCKING_FD_SMOKE` 宏及对应 COM1 标记。不新增 syscall 编号、不改 syscall
ABI、不改默认启动行为。

## 改动范围

内核与头文件：

- `include/bigos/fs/vfs.h`：新增 `OPEN_NONBLOCK = 1<<11`（O_NONBLOCK 唯一内核来源）
  与 `OPEN_ACCMODE`；在 `File` 末尾追加 `bool nonblocking`（既有布局不重排，
  `static_assert` 偏移守卫锁定 `nonblocking` 在 `identity` 之后、`identity` 在
  `writable` 之后）；新增内核内辅助 `file_is_nonblocking(File*)` 与
  `file_access_mode(File*)`。
- `kernel/core/fs/vfs.cc`：exFAT/bigfs 两处 `File` 创建点显式初始化
  `nonblocking = false`。
- `kernel/core/ipc/pipe.cc`：`pipe_read` 空管道、`pipe_write` 满管道判定点把短路
  条件扩展为 `file_is_nonblocking(file) || !can_block()`；两处 `File` 创建点初始化
  `nonblocking = false`。
- `kernel/core/terminal/tty.cc`：`tty_read` 保留 `!can_block()` 严格短路，新增
  `nonblocking && !input_available` 的 would-block 出口（复用 `tty_poll` 同源谓词，
  不出队、不改输入环）；`create_tty_file` 初始化 `nonblocking = false`。
- `kernel/core/net/socket.cc`：`socket_create` 初始化 `nonblocking = false`。
- `include/bigos/proc.h`：新增 `FCNTL_F_GETFL = 3`/`FCNTL_F_SETFL = 4` 与
  `O_NONBLOCK = bigos::vfs::OPEN_NONBLOCK`；新增 `nonblocking_fd_smoke_entry` 声明。
- `kernel/core/proc/proc.cc`：`fcntl_fd_current` 实现 `F_GETFL`（合成访问模式 |
  O_NONBLOCK）与 `F_SETFL`（单条 `nonblocking = (arg & O_NONBLOCK) != 0`，忽略
  其余位、返回 0）；stdio 安装路径注释补充终端 fd 0/1/2 联动契约；新增
  `nonblocking_fd_smoke_*` 入口（pipe/tty/socket 三段断言）。
- `kernel/core/syscall/syscall.cc`：`sys_recvfrom` 非阻塞 fd 取 `recv_rounds = 1`
  且跳过 `yield`，无数据经 `NoData -> -EAGAIN`；`sys_fcntl` guard 注释说明仅
  `F_DUPFD` 需 `can_block()`。
- `kernel/core/kernel.cc`：新增 `#ifdef BIGOS_NONBLOCKING_FD_SMOKE` spawn
  入口线程。

用户 libc：

- `user/libc/include/fcntl.h`：新增 `O_NONBLOCK`（`1<<11`，与内核同值）、
  `F_GETFL = 3`、`F_SETFL = 4`，更新头注释说明有界子集。
- `user/libc/syscall.c`：`fcntl` wrapper 对 `F_SETFL` 透传可变参（连同
  `F_DUPFD`/`F_SETFD`），`F_GETFL` 不取参传 0。

构建、测试与文档：

- `xmake/options.lua`：新增默认关闭 `nonblocking_fd_smoke` 选项。
- `xmake/kernel.lua`：映射到 `BIGOS_NONBLOCKING_FD_SMOKE` 宏。
- `tools/bigosdev/core.py`：`SMOKE_OPTIONS` 注册 `nonblocking_fd_smoke`。
- `tests/test_syscall_entry_source.py`：新增
  `test_nonblocking_fcntl_constants_match_across_kernel_and_user`，断言内核与
  user libc 的 `O_NONBLOCK`/`F_GETFL`/`F_SETFL` 取值一致且与 open flag 不冲突。
- `docs/en/arch/nonblocking-fd-io.md` + `docs/zh/arch/nonblocking-fd-io.md`：
  新增同相对路径的双语说明（标志、`F_GETFL`/`F_SETFL`、各后端 would-block 短路、
  socket recvfrom 语义、就绪一致性契约、默认关闭 smoke、非目标）。

## 已通过的检查

- `xmake`（x86_64-elf-gcc 默认交叉构建，`--nonblocking_fd_smoke=n`）：编译通过，
  默认启动路径不变。
- `xmake f --nonblocking_fd_smoke=y && xmake`：smoke 配置编译通过。
- QEMU headless smoke（smoke 开）：
  `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial-nonblocking.log --expect-serial-marker BIGOS_NONBLOCKING_FD_PASSED`
  → 观察到 `BIGOS_NONBLOCKING_FD_PASSED`（serial 日志第 13 行），无 `_FAILED`。
- QEMU headless 默认启动（smoke 关）：
  `--expect-serial-marker BIGOS_USER_EXEC` → 观察到 `BIGOS_USER_EXEC`，默认进入
  userland 行为未变。
- clang 辅助静态检查（freestanding C++17、`--target=x86_64-elf`、no hosted
  runtime、`-fno-exceptions`、`-fno-rtti`、项目 include；smoke 文件带
  `-DBIGOS_NONBLOCKING_FD_SMOKE`）：`include/bigos/fs/vfs.h`（随各 .cc）、
  `kernel/core/ipc/pipe.cc`、`kernel/core/terminal/tty.cc`、`kernel/core/fs/vfs.cc`、
  `kernel/core/net/socket.cc`、`kernel/core/syscall/syscall.cc`、
  `kernel/core/proc/proc.cc` 均 0 error、0 本次新增告警；`user/libc/syscall.c`
  （C11 freestanding）0 error。
- Python 校验：`uv run pytest tests/test_syscall_entry_source.py` → 19 passed
  （含新增契约测试）。全量 `uv run pytest` → `20 failed, 326 passed`；经
  `git stash` 移除本次改动后复跑为 `20 failed, 325 passed`，失败用例集合按
  `diff` 完全一致（本次新增 0 失败、+1 通过即新增契约测试）。

## 历史诊断（变更前已存在，非本次引入）

- `uv run pytest` 全量 20 个失败用例（如 `test_user_address_space_vmem_source`、
  `test_user_elf_program_loader_source`、`test_stable_file_growth_source`、
  `test_user_c_baseline_source` 等）为既有源码漂移，stash 前后失败集合完全一致，
  与本变更无关。其中 vmem/elf-loader 两例的失败字符串会显示 `proc.cc` 末尾本次
  新增的 smoke 文本，但其断言目标（`activate_address_space_root`、
  `create_elf_user_process(prepared, ...)` 等）是变更前即已漂移的既有内容，非本次
  引入。

## 因环境无法独立运行 / 残留风险

- clangd（9.4 范围内的辅助诊断）：其诊断与 `compile_commands.json` 驱动的 clang
  `-fsyntax-only` 等价，已被上述 clang 检查覆盖；未单独跑 clangd LSP 会话。权威
  检查仍是 GCC 交叉构建。
- socket 非阻塞 recvfrom 的用户态端到端：`sys_recvfrom` 需经 `int 0x80` 携带
  已验证用户缓冲区，无法从内核线程 smoke 直接驱动。smoke 改为在真实 socket fd 上
  验证 `F_GETFL`/`F_SETFL` 往返、`file_is_nonblocking` 置位、以及空接收队列下
  `poll_file` 不可读（即非阻塞 recvfrom 会 `-EAGAIN` 的状态）；`recv_rounds=1` 与
  跳过 `yield` 的分支已由源码与构建覆盖。残留风险：跨进程用户态非阻塞 recvfrom
  的实际 `-EAGAIN` 返回需后续用户态 smoke 或多路复用接入时端到端回归。
- 终端非阻塞读的真实键盘输入路径：smoke 在无输入态验证 would-block 与 fd 0/1/2
  联动；未人工敲入键盘验证“有输入时非阻塞读立即返回”。残留风险：交互输入回显路径
  需人工或带输入能力的 emulator 回归。

## 本次变更引入并已解决的问题

- `File` 末尾追加 `nonblocking` 后，最初的偏移守卫写成依赖 `sizeof(FileIdentity)`
  的算术等式，在对齐/填充下脆弱。已改为基于 `__builtin_offsetof` 的顺序比较守卫
  （`nonblocking > identity > writable`），GCC 默认构建与 clang 语法检查双通过。
- `tty_read` 最初把 `!can_block()` 与 `nonblocking` 合并为单一“无输入即短路”
  条件，可能改变不可阻塞上下文下“有输入仍按既有路径消费”的既有行为。已拆分为
  先保留严格 `!can_block()` 短路、再追加 `nonblocking && !input_available` 出口，
  保持既有行为不变。
