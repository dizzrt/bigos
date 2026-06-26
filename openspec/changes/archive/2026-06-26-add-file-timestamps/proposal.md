## Why

当前 BigOS 的 metadata 契约只暴露 type/mode/uid/gid/nlink/size/object_id，明确不承诺完整时间戳语义，因此 `touch` 不能正确实现“创建或更新时间戳”。补齐 `atime`、`mtime`、`ctime` 与 `utime`/`utimens` 类接口，可以让文件状态观察、构建工具、测试脚本和日常用户工具有一个有界但真实的时间戳基础。

## What Changes

- 扩展内核与用户态 metadata ABI，新增秒级 `st_atime`、`st_mtime`、`st_ctime` 字段，保持固定宽度、完全初始化和用户缓冲校验。
- 为 `/rw` bigfs 维护 inode 时间戳：
  - create/mkdir 初始化 atime/mtime/ctime 为当前 wall-clock 秒。
  - read 可更新 atime。
  - write/truncate 更新 mtime 与 ctime。
  - chmod/chown 若未来存在，应更新 ctime；本变更只为当前已有 metadata mutation 定义 ctime。
  - rename/unlink/rmdir 更新受影响目录的 mtime/ctime；目标对象 ctime 是否变化按设计明确。
- 增加有界 `utime`/`utimens` syscall 与 libc wrapper，用于显式设置文件 atime/mtime；成功后更新目标 ctime。
- 增加用户态 `touch` 工具：缺省创建不存在文件或更新已存在文件 atime/mtime 到当前时间；支持一个有界显式时间参数可选项时，只承诺 BigOS 文档定义格式。
- 更新 `stat` 输出，让用户可以观察 atime/mtime/ctime。
- 增加行为验证，覆盖 create/read/write/truncate/utimens/touch/stat 的时间戳变化。
- 非目标：不实现纳秒精度、时区/locale 格式化、POSIX 完整 `utimensat`、`lutimes`、符号链接时间戳、挂载选项如 `noatime`、完整权限修改、跨系统时间同步或 release-grade time conversion 库。

## Capabilities

### New Capabilities

- `file-timestamps`: 定义 BigOS 有界文件时间戳 ABI、更新规则、显式更新时间 syscall/libc 和验证边界。

### Modified Capabilities

- `file-metadata-contract`: metadata 从“无完整时间戳语义”扩展为暴露有界 atime/mtime/ctime。
- `writable-filesystem`: `/rw` bigfs inode 与目录操作需要维护时间戳。
- `bounded-syscall-surface`: 追加 `utime`/`utimens` 类 syscall，不改动已有 syscall 编号。
- `user-libc-min`: 暴露用户态 `utime`/`utimens` wrapper 与必要类型。
- `userland-path-tools`: `stat` 显示时间戳，新增 `touch` 作为默认外部路径工具。

## Impact

- 影响内核 metadata ABI：`include/bigos/metadata.h`、`kernel/core/fs/vfs.cc`、`kernel/core/proc/proc.cc`、syscall dispatch。
- 影响 bigfs 后端：on-disk/in-memory inode 结构、format version 或兼容迁移、create/read/write/truncate/rename/unlink/rmdir/fsync 路径。
- 影响用户 libc：`user/libc/include/sys/stat.h`、`user/libc/include/unistd.h` 或新增 `utime.h`、`sys_nr.h`、`syscall.c`。
- 影响用户工具与打包：`user/bin/stat.c`、新增 `user/bin/touch.c`、`xmake/user_package.lua`、`tools/bigosdev/core.py`。
- 影响验证：需要源码 ABI 检查、用户程序构建、QEMU/Bochs 可用时的用户态 runtime smoke。不会改变 boot 地址、链接地址、IDT/syscall vector、页表布局、CR3 切换或磁盘分区布局。
