## 1. Metadata ABI

- [x] 1.1 更新 `include/bigos/metadata.h` 和 `user/libc/include/sys/stat.h`，显式增加 atime/mtime/ctime 秒级字段。
- [x] 1.2 增加或更新 source contract 测试，确保内核 metadata 与用户 `struct stat` 字段顺序、宽度和 syscall mirror 一致。
- [x] 1.3 更新 `kernel/core/fs/vfs.cc` 的 exFAT/bigfs metadata 填充逻辑，保证时间戳字段完全初始化。

## 2. bigfs 时间戳存储与更新

- [x] 2.1 评估并调整 `kernel/core/fs/bigfs.cc` inode 布局；必要时 bump bigfs format version 并定义旧格式拒绝/重格式化策略。
- [x] 2.2 在 create/mkdir/read/write/truncate/rename/unlink/rmdir/fsync 相关路径维护 atime/mtime/ctime 和 dirty metadata。
- [x] 2.3 为失败路径补充回滚或不发布策略，确保失败操作不留下半更新 timestamp。

## 3. Syscall 与 libc

- [x] 3.1 追加文件时间戳 syscall 编号、dispatch 和内核实现，保持现有 syscall 编号不变。
- [x] 3.2 增加 libc wrapper、头文件声明、NOW/OMIT 常量或 BigOS-specific flags，并保持 errno 翻译一致。
- [x] 3.3 覆盖只读后端、权限、非法用户指针、非法 flags、缺失路径和 unsupported object 的错误映射。

## 4. 用户工具与打包

- [x] 4.1 更新 `/bin/stat` 输出 atime/mtime/ctime 数字字段。
- [x] 4.2 新增 `/bin/touch`，支持创建缺失文件和更新已有文件 atime/mtime 到当前时间。
- [x] 4.3 更新 `xmake/user_package.lua` 与 `tools/bigosdev/core.py`，默认构建并打包 `/bin/touch`。

## 5. 文档与规格同步

- [x] 5.1 更新 `docs/en` 和 `docs/zh` 中 userland/runtime/filesystem metadata 相关说明，保持中英文镜像同步。
- [x] 5.2 更新 OpenSpec 相关既有能力描述或验证说明，移除“无完整时间戳语义”的过期表述并保留 bounded timestamp 边界。

## 6. 验证

- [x] 6.1 运行 `openspec validate add-file-timestamps --strict`。
- [x] 6.2 运行最窄用户程序构建检查，确认 libc、`stat`、`touch` 和打包清单可编译。
- [x] 6.3 运行相关 Python source tests；如修改 Python 文件，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest`。
- [x] 6.4 环境允许时运行 QEMU headless 用户态 smoke，验证 create/read/write/truncate/utimens/touch/stat 时间戳行为；不可用时记录缺失依赖和剩余风险。
