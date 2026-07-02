# Validation

## 已通过

- 工具链可用性：已确认 `xmake`、`x86_64-elf-gcc`、`x86_64-elf-g++`、`x86_64-elf-ld`、`qemu-system-x86_64`、`uv`、`clang`、`clangd` 可用。
- 默认构建：`xmake f -c && xmake` 通过。
- stream socket 构建：`xmake f --stream_socket_smoke=y && xmake` 通过。
- TCP 协议层回归构建：`xmake f --tcp_path_smoke=y && xmake` 通过。
- clang 辅助静态检查：对 `kernel/core/net/socket.cc`、`kernel/core/net/tcp.cc`、`kernel/core/ipc/pipe.cc`、`kernel/core/syscall/syscall.cc`、`kernel/core/signal/signal.cc` 运行 freestanding C++17 `clang --target=x86_64-elf -fsyntax-only -ffreestanding -fno-rtti -fno-exceptions -DBIGOS_USER_PROCESS -DBIGOS_STREAM_SOCKET_SMOKE -Iinclude -Icpp/include -Icpp/libsupc++/include`，无新增诊断。
- QEMU headless stream socket smoke：`uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/stream_socket_smoke.log --expect-serial-marker BIGOS_STREAM_SOCKET_PASSED` 观察到 `BIGOS_STREAM_SOCKET_PASSED`。
- QEMU headless 默认启动回归：`uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/default_boot.log --expect-serial-marker BIGOS_USER_EXEC` 观察到 `BIGOS_USER_EXEC`。
- QEMU headless TCP path 回归：`--tcp_path_smoke=y` 下观察到 `BIGOS_TCP_PATH_PASSED`。
- QEMU headless pipe 回归：`--pipe_smoke=y` 下观察到 `BIGOS_PIPE_PASSED`。
- QEMU headless signal 回归：`--signal_smoke=y` 下观察到 `BIGOS_SIGNAL_PASSED`。
- 源码契约：`uv run pytest tests/test_syscall_entry_source.py tests/test_bounded_tcp_path_source.py tests/test_signals_source.py -q` 通过，覆盖 syscall number 双份一致、errno 镜像、SIGPIPE 镜像与 stream socket ABI 契约。

## 已知基线失败 / 非本变更新增

- `uv run pytest tests/ -q` 仍有 20 个历史源码契约失败。通过 stash 当前变更后复跑确认这些失败在基线同样存在，集中在既有 stable file growth、user address-space、userland smoke、user ELF loader 等历史断言漂移上；本变更新增失败已修复。

## 本变更引入并已解决的问题

- `tests/test_bounded_tcp_path_source.py` 原先固化 “bounded TCP path 不新增 socket ABI” 的旧非目标。当前 `stream-socket-interface` 变更显式新增 `SYS_CONNECT`/`SYS_LISTEN`/`SYS_ACCEPT`/`SYS_GETSOCKOPT`/`SYS_SEND`，因此已把该测试更新为校验新的有界 stream socket ABI，同时仍禁止 broad `SYS_TCP`。
- `--pipe_smoke=y` 独立构建暴露 `kernel/core/kernel.cc` 中 pipe smoke 对 `strlen` 的历史隐式 include 依赖；已在 `BIGOS_PIPE_SMOKE` include guard 下补 `string.h`，随后 pipe smoke 通过。

## 行为变化记录

- pipe broken-pipe 行为从“仅返回 `-EPIPE`，`SIGPIPE` 可选”更新为必需投递 `SIGPIPE`：当所有读端关闭后写端写入，内核通过统一 `bigos::signal::raise_broken_pipe(process, suppress)` helper 返回 `-EPIPE` 并按 `SIG_IGN` 抑制规则投递 `SIGPIPE`。stream socket broken-pipe 与 pipe 共用该 helper；`send(..., MSG_NOSIGNAL)` 对 stream socket 单次调用抑制 `SIGPIPE`。

## 残留风险

- stream socket smoke 当前是 kernel/backend 闭环验证，覆盖协议 TCB、stream socket ops、read/write/poll、EOF、EPIPE、MSG_NOSIGNAL、reset 等核心路径；完整用户态端到端 wrapper 流程仍主要由源码契约和 syscall 构建覆盖，未新增独立用户程序进行真实 ring3 `connect`/`listen`/`accept` 脚本交互。
- `getsockopt(SO_ERROR)` 仅实现 `SOL_SOCKET/SO_ERROR`，其它 option 按设计返回 `-ENOPROTOOPT`；未扩展 option 矩阵。
