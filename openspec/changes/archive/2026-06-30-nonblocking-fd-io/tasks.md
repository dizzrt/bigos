# nonblocking-fd-io 任务清单

按子系统拆分，依赖顺序自上而下。涉及 `include`、`kernel/core`、`user/libc` 下的 C/C++ 改动，按规则附 clang/clangd 辅助静态检查与 QEMU headless smoke 验证；涉及源级契约校验的部分附 `uv run pytest` 任务。先确认 tty fd 的 `vfs::File` 共享关系，再落地非阻塞标志。

## 1. VFS 非阻塞标志与就绪基础

- [x] 1.1 在 `include/bigos/fs/vfs.h` 的 `vfs::File` 末尾追加 `bool nonblocking`（默认 false），不重排既有布局（沿用 `bool writable;` 先例），以 `_Static_assert` 或源级核对守护字段偏移。
- [x] 1.2 在 `include/bigos/fs/vfs.h` 提供内核内辅助：读取非阻塞标志（如 `bool file_is_nonblocking(File*)`）与由 `readable`/`writable` 合成访问模式位的助手，供 fd-control 与各后端复用。
- [x] 1.3 定义内核内 `O_NONBLOCK` 常量来源（与 `O_RDONLY/O_WRONLY/O_RDWR/O_CREAT/O_TRUNC` 取值不冲突），明确其值同时被 fd-control 与用户 libc 镜像使用。
- [x] 1.4 在 `kernel/core/fs/vfs.cc` 确认 `File` 创建/初始化路径把 `nonblocking` 初始化为 false，且 `dup`/`retain` 路径不复制出独立标志（共享同一 `File` 即共享标志）。

## 2. tty fd 联动语义（已确认）

- [x] 2.1 复核 `kernel/core/proc/proc.cc` 的 stdio 安装路径，确认 fd 0/1/2 共享同一 `vfs::File`（一次 `create_tty_file()` + fd 1/2 各 `vfs::retain`，ref_count == 3），从而非阻塞标志在终端 fd 间联动。
- [x] 2.2 把该联动语义（对 0/1/2 任一设 `O_NONBLOCK` 影响全部共享同一 OFD 的终端 fd，含 `dup` 副本，符合 POSIX 共享 OFD 语义）写入文档并作为 smoke 断言预期，不改变安装方式、不下放标志到 `FdEntry`。

## 3. fd-control 扩展（F_GETFL/F_SETFL）

- [x] 3.1 在 `include/bigos/proc.h` 新增 `FCNTL_F_GETFL`/`FCNTL_F_SETFL` 命令常量（取值不与 `F_DUPFD=0/F_GETFD=1/F_SETFD=2` 冲突）与 `O_NONBLOCK`（与 1.3 同值）。
- [x] 3.2 在 `kernel/core/proc/proc.cc` 的 `fcntl_fd_current` 实现 `F_GETFL`：返回合成访问模式位按位或 `O_NONBLOCK`（标志置位时），不改 fd/offset/引用/close-on-exec。
- [x] 3.3 在 `fcntl_fd_current` 实现 `F_SETFL`：实现为单条 `nonblocking = (arg & O_NONBLOCK) != 0`，忽略 `arg` 的其余所有位（访问模式 `O_RDONLY`/`O_WRONLY`/`O_RDWR`、创建位 `O_CREAT`/`O_TRUNC`、未实现 status 位）且不报错、返回 0，不触碰 `close_on_exec`；确保标准惯用法 `F_SETFL(F_GETFL | O_NONBLOCK)`（`arg` 携带访问模式位）不被误判为 `-EINVAL`。
- [x] 3.4 复核 `kernel/core/syscall/syscall.cc` 的 `sys_fcntl`：`F_GETFL`/`F_SETFL` 为纯标志读写，不加 `can_block()` guard（该 guard 仅服务可能增长 fd 表的 `F_DUPFD`）。

## 4. pipe 非阻塞集成

- [x] 4.1 在 `kernel/core/ipc/pipe.cc` 的 `pipe_read` 空管道判定点，把短路条件从 `!can_block()` 扩展为 `file_is_nonblocking(file) || !can_block()`，返回 `Status::WouldBlock`，复用 `read_ready` 同源谓词。
- [x] 4.2 在 `pipe_write` 满管道判定点同样扩展短路条件，复用 `write_ready`；已写出部分字节时按既有 `done > 0` 返回已写字节而非 would-block。

## 5. tty 非阻塞集成

- [x] 5.1 在 `kernel/core/terminal/tty.cc` 读路径无输入判定点扩展短路条件为 `file_is_nonblocking(file) || !can_block()`，返回 `WouldBlock`，复用 `input_available`/`input_record_available` 同源谓词，且不出队输入记录、不改输入环。
- [x] 5.2 确认终端写出方向恒可写、非阻塞不改变其行为。
- [x] 5.3 确认 tty 非阻塞标志存于共享的 `vfs::File`：对终端 fd（如 fd 1）经 `F_SETFL` 设非阻塞后，共享同一 OFD 的 fd 0 与 `dup` 副本读路径同步生效（联动），不引入裸 fd 特例。

## 6. socket 非阻塞集成

- [x] 6.1 在 `kernel/core/syscall/syscall.cc` 的 `sys_recvfrom`，当 fd 非阻塞时把 RX 推进轮次设为 1 且跳过 `sched::yield()`，无数据返回 `-EAGAIN`。
- [x] 6.2 复核阻塞 socket fd 的 `RECV_MAX_ROUNDS`/`yield`/返回码保持不变，非阻塞分支不影响成功路径的数据搬运与地址回写。

## 7. 用户 libc 镜像

- [x] 7.1 在 `user/libc/include/fcntl.h` 增补 `O_NONBLOCK`（与内核同值）、`F_GETFL`、`F_SETFL` 常量，并更新头注释说明新增的有界 `O_NONBLOCK` 子集。
- [x] 7.2 复核 `user/libc/syscall.c` 的 `fcntl` wrapper 对 `F_SETFL`/`F_GETFL` 的可变参透传与返回值/ errno 翻译，保持 freestanding-safe。

## 8. 运行期 smoke 验证

- [x] 8.1 在 `xmake/options.lua` 新增默认关闭开关（如 `nonblocking_fd_smoke`），在 `xmake/kernel.lua` 映射到 `BIGOS_NONBLOCKING_FD_SMOKE` 宏，遵循既有 smoke 选项模式。
- [x] 8.2 在 `kernel/core/kernel.cc` 的 smoke 阶段新增 `#ifdef BIGOS_NONBLOCKING_FD_SMOKE` 入口线程：对 pipe/tty/socket 断言「置位 `O_NONBLOCK` 后无数据读/满缓冲写返回 would-block」「清除标志后恢复阻塞」「`F_GETFL`/`F_SETFL` 往返一致」「`F_SETFL(F_GETFL | O_NONBLOCK)` 携带访问模式位时仍成功、只改非阻塞位、不返回 `-EINVAL`」「就绪查询可读 ⇔ 非阻塞读不 would-block」，并断言终端 fd 联动（对一个终端 fd 设 `O_NONBLOCK` 后，共享同一 OFD 的另一终端 fd 与 `dup` 副本经 `F_GETFL` 同步可见非阻塞位、读路径同步生效），发 COM1 `BIGOS_NONBLOCKING_FD_PASSED`/`_FAILED` 标记。
- [x] 8.3 在 smoke 线程 bring-up 处按既有模式以 `#ifdef` 守卫 spawn 该入口线程。

## 9. 构建与静态检查

- [x] 9.1 运行 `xmake`（x86_64-elf-gcc 交叉构建）确认默认配置编译通过且默认启动行为不变；若交叉工具链不可用则显式记录该阻塞与残留风险。
- [x] 9.2 运行 `xmake f --nonblocking_fd_smoke=y && xmake` 确认 smoke 配置编译通过。
- [x] 9.3 对新增/修改的 C/C++ 文件执行 clang 辅助静态检查，配置尽量贴近 GCC 交叉环境（freestanding C++17、x86_64 target、项目 include 路径、no hosted runtime、no exceptions、no RTTI）；修复本次变更引入的 clang 错误，确认或修复有效新增告警。若等价 clang 标志不可用则记录差距与残留风险。
- [x] 9.4 对新增/修改的 C/C++ 文件执行 clangd 辅助诊断；区分历史诊断、本次变更诊断与 freestanding/工具链误报，修复本次引入的问题。

## 10. 源级契约与仿真器 smoke

- [x] 10.1 更新/扩展 `tests/test_syscall_entry_source.py`（或相邻源契约测试）断言内核与 `user/libc` 的 `O_NONBLOCK`/`F_GETFL`/`F_SETFL` 取值一致且与既有 open flag/fcntl cmd 不冲突；运行 `uv run pytest tests/test_syscall_entry_source.py`，`uv` 不可用时显式记录该阻塞。
- [x] 10.2 通过 QEMU headless 路径运行 smoke（启用 `nonblocking_fd_smoke`）并期待 COM1 `BIGOS_NONBLOCKING_FD_PASSED`，例如 `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial.log --expect-serial-marker BIGOS_NONBLOCKING_FD_PASSED`；若 QEMU/工具链/镜像不可用则显式记录跳过原因与残留风险。

## 11. 文档与验证记录

- [x] 11.1 在 `docs/en` 更新非阻塞描述符行为说明（`vfs::File` 非阻塞标志、`F_GETFL`/`F_SETFL`、各后端 would-block 短路、socket recvfrom 语义、与就绪模型一致性、默认关闭 smoke），并在 `docs/zh` 同步对应相对路径镜像；使用仓库相对路径，不暗示 boot/链接/向量/页表/磁盘/ABI 变更。
- [x] 11.2 整理验证记录，分别列出已通过的检查、因环境无法运行的检查（含原因与残留风险）、历史诊断与本次变更引入的问题。
