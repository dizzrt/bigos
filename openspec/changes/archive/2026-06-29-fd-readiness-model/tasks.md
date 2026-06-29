# fd-readiness-model 任务清单

按子系统拆分，依赖顺序自上而下。涉及 kernel/core、include 下的 C++ 改动，按规则附 clang/clangd 辅助静态检查与 QEMU headless smoke 验证。本变更不新增/不依赖 Python 文件，故不含 Python 校验任务。

## 1. VFS 统一就绪入口与位标志

- [x] 1.1 在 `include/bigos/fs/vfs.h` 末尾追加可选就绪操作（如 `uint32_t (*poll)(File*) noexcept`）到 `FileOperations`，不重排既有 `read/close/write/lseek/truncate/readdir` 槽位；以 `_Static_assert` 或源级核对避免布局被错误重排。
- [x] 1.2 在 `include/bigos/fs/vfs.h` 定义内核内就绪位常量（可读/可写/错误），明确为内核内部约定、不进入用户 syscall ABI。
- [x] 1.3 在 `kernel/core/fs/vfs.cc` 实现统一入口 `vfs::poll_file(File*)`：`ops->poll` 非空时调用之，否则返回确定性默认（可读+可写、无错误），保证纯只读快照、不阻塞、不出队、不改打开状态。

## 2. pipe 后端就绪接入

- [x] 2.1 在 `kernel/core/ipc/pipe.cc` 实现 pipe 的 `poll` op，复用既有 `read_ready`/`write_ready` 与 `read_open`/`write_open`，映射可读（含写端关闭 EOF）、可写、读端关闭时的写端错误位（broken pipe 倾向）。
- [x] 2.2 把 `poll` op 挂接到 `PIPE_READ_OPS`/`PIPE_WRITE_OPS`，确保就绪查询结果与既有阻塞读写谓词同源一致。

## 3. socket/UDP 等待队列与就绪接入

- [x] 3.1 在 `include/bigos/net.h`/`include/bigos/net/socket.h` 的 `UdpEndpoint`（或 `Socket`）增加接收等待队列字段（`sched::WaitQueue rx_wait`），并在端点初始化处 `init_wait_queue`。
- [x] 3.2 在协议 RX 投递路径（`kernel/core/net/protocol.cc` 的 UDP demux/`pump`/`inject_frame`）把 datagram 放入 `rx_queue` 之后调用 `sched::wake_all`/`wake_one`，保持分配无关、IRQ/投递上下文安全（先入队再唤醒）。
- [x] 3.3 在 `kernel/core/net/socket.cc` 实现 socket 的 `poll` op：`rx_count > 0` → 可读，端点可发送有效状态 → 可写，端点失活/未绑定 → 错误位，并挂接到 `SOCKET_OPS`。
- [x] 3.4 复核 `sys_recvfrom`（`kernel/core/syscall/syscall.cc`）对外语义保持不变（无数据且不可阻塞仍返回 `-EWOULDBLOCK`）；本任务仅补齐等待队列与就绪查询，不改变其返回码与轮询契约。

## 4. terminal/tty 就绪接入

- [x] 4.1 在 `kernel/core/terminal/tty.cc` 为既有 `TTY_OPS` 实现 poll op（填入预留槽位），复用 `input_available`/`input_record_available`：有输入或挂起转义字节 → 可读，写出方向恒可写，输入路径不置错误位。
- [x] 4.2 确保 poll op 为只读查询，不出队任何 `TerminalInputRecord`、不改动输入环 head/tail；确认 fd 0/1/2 及 `dup` 副本因共享同一 `TTY_OPS` 句柄而就绪一致。

## 5. 运行期 smoke 验证

- [x] 5.1 在 `xmake/options.lua` 新增默认关闭开关（如 `fd_readiness_smoke`），在 `xmake/kernel.lua` 映射到 `BIGOS_FD_READINESS_SMOKE` 宏，遵循既有 smoke 选项模式。
- [x] 5.2 在 `kernel/core/kernel.cc` 的 smoke 阶段新增 `#ifdef BIGOS_FD_READINESS_SMOKE` 入口线程：分别构造 pipe、socket、tty 三类描述符，断言“查询可读后阻塞读立即返回/查询不可读则无数据”等一致性，并断言对共享 `TTY_OPS` 的另一终端 fd（`dup`）就绪一致，发 COM1 `BIGOS_FD_READINESS_PASSED`/`_FAILED` 标记。
- [x] 5.3 在 smoke 线程 bring-up 处按既有模式 spawn 该入口线程（`#ifdef` 守卫）。

## 6. 构建与静态检查

- [x] 6.1 运行 `xmake`（x86_64-elf-gcc 交叉构建）确认默认配置编译通过且默认启动行为不变；若交叉工具链不可用则显式记录该阻塞与残留风险。
- [x] 6.2 运行 `xmake f --fd_readiness_smoke=y && xmake` 确认 smoke 配置编译通过。
- [x] 6.3 对新增/修改的 C++ 文件执行 clang 辅助静态检查，配置尽量贴近 GCC 交叉环境（freestanding C++17、x86_64 target、项目 include 路径、no hosted runtime、no exceptions、no RTTI）；修复本次变更引入的 clang 错误，确认或修复有效新增告警。若等价 clang 标志不可用则记录差距与残留风险。
- [x] 6.4 对新增/修改的 C++ 文件执行 clangd 辅助诊断；区分历史诊断、本次变更诊断与 freestanding/工具链误报，修复本次引入的问题。

## 7. 仿真器 smoke 与文档

- [x] 7.1 通过 QEMU headless 路径运行 smoke（启用 `fd_readiness_smoke`）并期待 COM1 `BIGOS_FD_READINESS_PASSED`，例如 `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial.log --expect-serial-marker BIGOS_FD_READINESS_PASSED`；若 QEMU/工具链/镜像不可用则显式记录跳过原因与残留风险。
- [x] 7.2 在 `docs/en` 更新内核 fd 就绪模型说明（统一入口、就绪位、各后端语义、socket 等待队列、tty 桥接、默认关闭 smoke），并在 `docs/zh` 同步对应相对路径的中文镜像；使用仓库相对路径，不暗示 boot/链接/向量/页表/磁盘/ABI 变更。
- [x] 7.3 整理验证记录，分别列出已通过的检查、因环境无法运行的检查（含原因与残留风险）、历史诊断与本次变更引入的问题。
