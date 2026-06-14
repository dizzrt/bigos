## Validation Notes

本记录覆盖本次实现会话中实际执行的检查，未把未运行项标记为通过。

## Passed

- `uv run pytest tests/test_fd_vfs_shell_source.py tests/test_writable_fs_page_cache_pipe_source.py`
  - 结果：27 passed。
  - 覆盖：VFS `-ERANGE` 映射、syscall 目录枚举缓冲不足返回、`bigfs` 新 inode 发布失败清理源码契约、`bigfs::write()` staging 提交/rollback 源码契约、统一错误映射记录、libc/shell/tool 用户可观察边界、fd/VFS blocking guard 审查记录、后端分派与 open file 引用语义记录、metadata path/fd/error/non-goal contract 记录。
- `xmake`
  - 结果：默认配置 build ok。
  - 覆盖：修改过的 kernel C++ 源和头文件可通过当前 xmake/cross-toolchain 构建，包括新增 `ENODEV`/`EOPNOTSUPP` errno 名称和 syscall 注释。
- `clang++ -fsyntax-only -target x86_64-elf -std=c++17 -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -DBIGOS_USER_PROCESS -Iinclude -Icpp/include -Icpp/libsupc++/include kernel/core/fs/bigfs.cc kernel/core/fs/vfs.cc kernel/core/syscall/syscall.cc`
  - 结果：通过，无输出。
  - 覆盖：本次修改过的主要 C++ 源在 clang 辅助检查下可解析；该检查不替代 xmake/cross GCC 链接。
- `xmake f --userland_smoke=y` 后执行 `xmake`
  - 结果：userland smoke 配置 build ok。
  - 覆盖：修改过的 userland smoke 和用户态 errno mirror 可参与 user-init ELF 构建。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/userland-smoke.log --expect-serial-marker BIGOS_USERLAND_PASSED`
  - 结果：QEMU headless 观察到 `BIGOS_USERLAND_PASSED`。
  - 覆盖：用户态 libc errno、`bigos_readdir(..., BIGOS_DIRENT_MAX_BATCH + 1)` 返回 `ERANGE`、`/rw` create/write/read/stat/list、越界写返回 `ENOSPC` 且 offset/size 保持、rename `EEXIST`、只读 exFAT `EROFS`、unlink/open-fd metadata、fork 继承 fd 共享 offset、独立 open offset、shell errno 输出组合路径。
- `uv run python tools/boot_debug.py run --emulator qemu --display none --serial-log build/test/writable-fs-smoke.log --expect-serial-marker BIGOS_WRITABLE_FS_PASSED`
  - 结果：QEMU headless 观察到 `BIGOS_WRITABLE_FS_PASSED`。
  - 覆盖：`/rw` create/write/read/fsync/cache eviction、owner/mode permission denied 返回 `AccessDenied`、只读 exFAT VFS 写请求返回 `ReadOnlyFs`。
- `GetDiagnostics`
  - 结果：无 IDE 诊断。
  - 覆盖：当前编辑文件未发现新增 clangd/IDE 报错。

## Not Run

- Bochs 交叉验证未运行。
  - 原因：本次使用 QEMU headless 作为最相关行为 marker 验证。
  - 残余风险：Bochs 特有 BIOS/IDE/显示路径差异未覆盖。

## Historical Issues

- 无本次会话新增历史失败记录。

## Current Change Issues

- `writable_fs_smoke` 曾因旧 `bigfs::open` 调用签名和缺少 `strlen` include 编译失败；已修复并通过 QEMU marker。

## Remaining Risk

- `/rw` 当前仍是 RAM-backed 当前运行期一致性，不承诺重启后持久化、journaling 或 exFAT 写入。
- 目录 execute/search bit、POSIX atomic replacement、完整目录 rename、ACL/xattr/symlink/device node 仍是非目标。
