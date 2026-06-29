# tty-as-file-descriptor 验证记录

本变更把全局终端表达为标准 `vfs::File`（设备/句柄分层），将 fd 0/1/2 安装为共享终端句柄，并把终端读写从裸 fd 特例收编到统一 `file->ops` 派发。实现过程中暴露并修复了 `/bin/sh` 交互检测与终端表达冲突的问题（改用 `isatty`/字符设备 `fstat`）。

## 改动范围

内核与头文件：

- `include/bigos/tty.h`：声明 `create_tty_file()` / `is_tty_file()`，并引入 `bigos/fs/vfs.h`。
- `kernel/core/terminal/tty.cc`：新增 `TTY_OPS`（`read`/`close`/`write`/`lseek`，预留 `poll` 接入点）、`create_tty_file()`（RDWR 句柄，`private_data` 指向全局 tty 设备层）、`is_tty_file()`；`tty_write` 内逐字节照搬原 `sys_write` fd 1/2 分支（`BIGOS_USER_WRITE_SYSCALL` marker、`SYS_WRITE_MAX_LEN` 上限、地址空间切换/还原、`default_terminal_write`）。
- `kernel/core/proc/proc.cc`：新增 `install_standard_fds()`（构造共享终端句柄装到 fd 0/1/2，`ref_count` 增至 3，`close_on_exec=false`，失败回滚）；在 `run_user_process()` 这一顶层 ring3 新进入点统一调用。fork 经 `clone_fd_table` retain、exec 仅 `close_on_exec_fds`、teardown 经 `close_all_fds` release，均沿用既有路径，无需特例。
- `kernel/core/syscall/syscall.cc`：删除 `sys_write`(fd 1/2) 与 `sys_read`(fd 0) 的 `file_for_fd_current(...)==nullptr` 裸 fd 特例，统一经 `write_fd_current`/`read_fd_current` → `file->ops`，保留 `can_block`、缓冲上限与 `EFAULT` 校验。
- `include/bigos/metadata.h`、`kernel/core/fs/vfs.cc`：`stat` 对终端句柄报告字符设备（`BIGOS_METADATA_TYPE_CHARDEV` / `S_IFCHR`，经 `is_tty_file` 识别），支撑用户态 `isatty`。
- `cpp/include/ext/aligned_buffer.h`：`nullptr_t` → `decltype(nullptr)`，消除 clang freestanding 头不兼容错误（GCC 仍正常）。

用户态：

- `user/libc/include/sys/stat.h`：增 `BIGOS_METADATA_TYPE_CHARDEV`、`S_IFCHR`、`S_ISCHR`。
- `user/libc/include/unistd.h` + `user/libc/syscall.c`：新增基于 `fstat` 的 `isatty()`（无新增 syscall 编号）。
- `user/sh/sh.c`：`is_interactive_session()` 改为 `isatty(0) && isatty(1)`，移除旧的 `fd_has_installed_file` 反向判断。

测试：

- `tests/test_tty_console_input_source.py`、`tests/test_first_user_program_source.py`、`tests/test_writable_fs_page_cache_pipe_source.py`、`tests/test_fd_vfs_shell_source.py`：把断言从“裸 fd 特例”更新到“终端经 File ops 派发 + marker 在 tty_write 内保留 + isatty 交互检测”的新结构。

## 已通过的检查

- `xmake`（x86_64-elf-gcc 默认交叉构建）：编译通过。
- QEMU headless 默认启动回归：
  - `uv run python -m tools.bigosdev run --emulator qemu --display none --serial-log logs/serial.log --expect-serial-marker BIGOS_USER_EXEC` → 观察到 `BIGOS_USER_EXEC`。
  - 同路径以 `--expect-serial-marker BIGOS_USER_WRITE_SYSCALL` 复跑 → 观察到该 marker；serial 顺序为 `BIGOS_INIT_ENTER` → `BIGOS_USER_ENTER` → `BIGOS_USER_EXEC` → `BIGOS_USER_WRITE_SYSCALL` → `$ ` 提示符，与变更前基线一致。
- clang 辅助静态检查（freestanding C++17、`--target=x86_64-elf`、项目 include、no rtti/exceptions/red-zone）：`kernel/core/terminal/tty.cc`、`kernel/core/syscall/syscall.cc`、`kernel/core/fs/vfs.cc`、`kernel/core/proc/proc.cc` 均 0 error。
- Python 校验：`uv run ruff check`（受影响测试无本次新增错误）、`uv run pyright`（0 error）、`uv run pytest`（全量 20 个历史失败，0 个本次新增失败；本变更直接更新的 tty/fd-vfs-shell/writable_fs 测试全部通过）。

## 历史诊断（变更前已存在，非本次引入）

- `uv run pytest` 全量存在 20 个失败用例，stash 对比确认变更前后数量与集合一致，均为既有源码漂移（如 `src/kernel` 旧文本、缺失的归档 proposal、TLB flush 措辞、libc 字符串、`user_mode = (__frame->cs & 0x3) == 0x3` 等），与本变更无关。
- `tests/test_tty_console_input_source.py` 有 4 处 `E501`（行过长）与 1 处 `ruff format` 重排建议，均落在本次未改动的历史长行上；本次新增断言无新增 ruff 问题。
- clang 对 `kernel/core/proc/proc.cc` 的 `-Wmissing-field-initializers` 告警落在既有 VMA 聚合初始化上（非本次新增代码），变更前后计数一致。

## 因环境无法独立运行 / 残留风险

- clangd（6.3 范围内的辅助诊断）：其结果与 `compile_commands.json` 驱动的 clang `-fsyntax-only` 等价，已通过上面的 clang 检查覆盖；未额外单独跑 clangd LSP 会话。权威检查仍是 GCC 交叉构建。
- 手工键盘交互（实际敲入命令、回显、Ctrl-C/EOF）：headless 自动回归只覆盖到提示符写出与 marker；交互输入路径未在本次做人工键盘验证。残留风险：实际输入回显/行编辑回归需后续人工或带输入能力的 emulator 验证。
- 重定向场景（`sh < file`、`a | b`）下的非交互判定：由 `isatty` 经 `fstat` 字符设备判断保证，已通过源码与构建覆盖逻辑正确性，但未在 headless 端到端单独构造重定向脚本回归。

## 本次变更引入并已解决的问题

- 终端表达为 `vfs::File` 后，`/bin/sh` 原“fd 0/1 无安装 File 即视为交互”的反向判断失效，导致提示符与回显消失、`BIGOS_USER_WRITE_SYSCALL` marker 丢失（QEMU headless 卡在 `BIGOS_USER_EXEC` 后无 `$`）。已通过“终端句柄 `fstat` 报告字符设备 + 用户态 `isatty` + shell 改用 `isatty(0) && isatty(1)`”修复，并经 QEMU headless 复测确认 marker 与提示符恢复。
- clang 对 `cpp/include/ext/aligned_buffer.h` 的 `nullptr_t` 报 `unknown type name`（GCC 容忍、clang 拒绝）。已改为 `decltype(nullptr)`，clang/GCC 双通过。
