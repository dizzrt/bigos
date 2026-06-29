# tty-as-file-descriptor 任务清单

按子系统拆分，依赖顺序自上而下。涉及 kernel/core、include 下的 C++ 改动，按规则附 clang/clangd 辅助静态检查与 QEMU headless 回归。涉及 `tests/*.py` 源码测试改动，附 `uv run` Python 校验任务。

## 1. 终端文件操作表与句柄构造（设备/句柄分层）

- [x] 1.1 在 `kernel/core/terminal/tty.cc`（必要时 `include/bigos/tty.h`）定义 `TTY_OPS`：`read` 复用 `read_char_blocking`/`read_raw_available_blocking`（按 `input_mode()` 选规范/原始模式），`close` 对设备层为 no-op；预留可选 `poll` 槽位的接入点（供后续 readiness 接入，本变更可留空）。
- [x] 1.2 在 `TTY_OPS.write` 中实现终端写出，逐字节照搬现有 `sys_write` fd 1/2 分支：保留 `serial_puts("BIGOS_USER_WRITE_SYSCALL\n")` + 内容、`SYS_WRITE_MAX_LEN` 上限、地址空间切换与还原、`default_terminal_write` 顺序等价。
- [x] 1.3 提供终端 File 构造 helper（`kmalloc` 分配 `vfs::File`，`ops=&TTY_OPS`、`vnode=nullptr`、`private_data` 指向全局 tty、`readable=writable=true`），并提供识别终端 File 的方式（按既有 ops 指针标识约定，类比 `is_pipe_file`）。

## 2. 进程 fd 表安装标准描述符

- [x] 2.1 在 `kernel/core/proc/proc.cc` 新增“安装标准 fd 0/1/2”helper：构造一个 RDWR 终端 File（`readable=writable=true`），安装到 fd 0/1/2（共享同一 File，`retain` 使 `ref_count` 增到 3，`close_on_exec=false`），失败时回滚已安装项（`release` 对应 retain）保持 fd 表一致。
- [x] 2.2 在用户进程创建路径统一调用该 helper（核对 `init_fd_table` 的约 5 处调用点：普通用户进程创建与各 smoke 注入进程），决定哪些路径需要标准 fd（保持有界）。
- [x] 2.3 核对 `fork` 复制 fd 表路径（`proc.cc` 约 2038 行的 `retain` 循环）对终端 File 正确 `retain`；核对 `dup`/`dup2`、进程退出/teardown 与 `close_on_exec_fds` 对终端 File 的 `release`，确保无重复释放、无泄漏。
- [x] 2.4 核对 `exec` 路径：`execve_current` 不重建 fd 表，仅调用 `close_on_exec_fds`（`proc.cc` 约 2851 行）；确认标准 tty fd 因 `close_on_exec=false` 在 exec 后天然保留、fd 0/1/2 仍可读写，exec 路径不调用安装 helper。

## 3. syscall 裸 fd 特例收编

- [x] 3.1 删除 `kernel/core/syscall/syscall.cc` 中 `sys_write` 的 `(__fd == 1 || __fd == 2) && file_for_fd_current(...)==nullptr` 特例分支，使 fd 1/2 写出统一经 `write_fd_current` → `file->ops`。
- [x] 3.2 删除 `sys_read` 的 `fd==0 && file_for_fd_current(0)==nullptr` 特例分支，使 fd 0 读取统一经 `read_fd_current` → `file->ops`；保留 `can_block`、缓冲上限、`EFAULT`/`EWOULDBLOCK` 等既有校验语义。

## 4. 源码测试同步

- [x] 4.1 更新 `tests/test_tty_console_input_source.py`：把断言从“裸 fd 特例”改为“终端经 File ops 派发 + marker 保留”，覆盖 `TTY_OPS.write` 内保留的 `BIGOS_USER_WRITE_SYSCALL`、内容输出与默认控制台写出。
- [x] 4.2 运行 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`（针对受影响测试），修复本次引入的 lint/类型/格式/用例问题；若 `uv` 不可用则显式记录该阻塞。

## 4b. 终端检测修订（实现暴露：`isatty` 取代“裸 fd”判断）

实现中发现 `/bin/sh` 用“fd 0/1 无安装 File”反向判断交互性，与终端表达为 `vfs::File` 冲突，导致提示符与回显消失、`BIGOS_USER_WRITE_SYSCALL` marker 丢失。以下为修复落地任务。

- [x] 4b.1 在 `include/bigos/metadata.h` 增加字符设备类型/mode（`BIGOS_METADATA_TYPE_CHARDEV`、`BIGOS_MODE_IFCHR`）；在 `kernel/core/fs/vfs.cc` 的 `stat` 中对 `terminal::is_tty_file()` 句柄报告字符设备（`S_IFCHR`），早于 `vnode == nullptr` 分支。
- [x] 4b.2 用户态新增 `isatty()`：`user/libc/include/sys/stat.h` 增 `S_IFCHR`/`S_ISCHR`，`user/libc/include/unistd.h` 增声明，`user/libc/syscall.c` 基于 `fstat` 实现（无新增 syscall 编号）。
- [x] 4b.3 `user/sh/sh.c` 的 `is_interactive_session()` 改为 `isatty(0) && isatty(1)`，移除旧的 `fd_has_installed_file` 反向判断；同步更新 `tests/test_fd_vfs_shell_source.py` 与 `tests/test_tty_console_input_source.py` 的相关断言。

## 5. 构建与静态检查

- [x] 5.1 运行 `xmake`（x86_64-elf-gcc 交叉构建）确认默认配置编译通过且默认启动行为不变；若交叉工具链不可用则显式记录阻塞与残留风险。
- [x] 5.2 对新增/修改的 C++ 文件执行 clang 辅助静态检查，配置尽量贴近 GCC 交叉环境（freestanding C++17、x86_64 target、项目 include 路径、no hosted runtime、no exceptions、no RTTI）；修复本次引入的 clang 错误，确认或修复有效新增告警。若等价 clang 标志不可用则记录差距与残留风险。
- [x] 5.3 对新增/修改的 C++ 文件执行 clangd 辅助诊断；区分历史诊断、本次变更诊断与 freestanding/工具链误报，修复本次引入的问题。

## 6. 仿真器回归与文档

- [x] 6.1 通过 QEMU headless 路径运行默认启动回归，确认既有 marker（如 `BIGOS_USER_WRITE_SYSCALL`、`BIGOS_USER_EXEC`）仍按既有顺序出现、终端读写与 shell 交互行为不变，例如 `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial.log --expect-serial-marker BIGOS_USER_EXEC`；若 QEMU/工具链/镜像不可用则显式记录跳过原因与残留风险。
- [x] 6.2 在 `docs/en` 更新终端/进程 I/O 文档，说明终端作为 `vfs::File`（设备/句柄分层）、标准 fd 0/1/2 安装与共享、读写经 ops 派发、引用计数与 marker 保留；在 `docs/zh` 同步对应相对路径的中文镜像；使用仓库相对路径，不暗示 boot/链接/向量/页表/磁盘/ABI 变更。
- [x] 6.3 整理验证记录，分别列出已通过的检查、因环境无法运行的检查（含原因与残留风险）、历史诊断与本次变更引入的问题。
