## Runtime Filesystem Semantics

本记录用于收口 `fd/VFS`、只读 exFAT、RAM-backed `/rw`、syscall、libc 和 shell/tool 可观察边界。它不扩展 POSIX ABI，只记录当前 change 要求的稳定负 errno、最小元数据和不可阻塞上下文约束。

## Error Mapping

fd/VFS 是用户可观察错误映射出口。backend 可以保留内部 enum，但 syscall 只能返回稳定负 errno，libc wrapper 只把负返回转换为 `errno = -ret` 和 `-1`，shell 与小工具只依赖 `errno` 数值或固定 shell 文案。

| 条件 | VFS/backend 源 | syscall 返回 | libc/tool/shell 可观察结果 | 状态边界 |
| --- | --- | --- | --- | --- |
| 缺失路径 | `Status::NotFound` / `FsStatus::NotFound` / `bigfs::Status::NotFound` | `-ENOENT` | `errno=ENOENT` 或工具输出 `errno=2` | 不发布 fd、目录项或 metadata |
| 已存在目标 | `Status::Exists` / `bigfs::Status::Exists` | `-EEXIST` | `errno=EEXIST` 或工具输出 `errno=17` | 不替换目标、不移除源 |
| 非法 fd | `Status::BadFileDescriptor` 或 fd table 直接失败 | `-EBADF` | `errno=EBADF` | 不修改 fd table 或 offset |
| 权限拒绝 | `Status::AccessDenied` / `bigfs::Status::AccessDenied` | `-EACCES` | `errno=EACCES` | 提交前拒绝，不修改内容、目录项、metadata、dirty block 或 offset |
| 只读后端拒写 | `Status::ReadOnlyFs` | `-EROFS` | `errno=EROFS` | 不修改 exFAT raw image、boot assets、mount state 或 cache dirty state |
| 后端容量耗尽 | `Status::NoSpace` / `bigfs::Status::NoSpace` | `-ENOSPC` | `errno=ENOSPC` | 不发布半成品目录项、inode 或 rename 目标 |
| 内核对象/页分配失败 | `Status::NoMemory` / `FsStatus::OutOfMemory` | `-ENOMEM` | `errno=ENOMEM` | 不发布 fd 或新对象 |
| 目录枚举输出容量不足 | `Status::Range` / `FsStatus::BufferTooSmall` 或 syscall batch 上限 | `-ERANGE` | `errno=ERANGE` | 不推进目录 offset，不发布 partial/uninitialized entries |
| 非法枚举参数 | `Status::InvalidArgument` 或 syscall `max_entries == 0` | `-EINVAL` | `errno=EINVAL` | 不修改目录或 fd 状态 |
| 非法用户缓冲 | syscall `validate_user_*` / copy failure | `-EFAULT` | `errno=EFAULT` 或当前进程 fault path | 不向用户缓冲复制 partial/uninitialized kernel data |
| 不可 seek 对象 | `Status::NotSeekable` | `-ESPIPE` | `errno=ESPIPE` | 不改变 offset |
| 不可 enumerate 对象 | `Status::NotDirectory` / `Status::IsDirectory` 按对象类型 | `-ENOTDIR` 或 `-EISDIR` | `errno=ENOTDIR` 或 `errno=EISDIR` | 不推进 offset，不写目录输出 |
| 后端 IO 失败 | `Status::BlockError` / `FsStatus::BlockError` / `bigfs::Status::IoError` | `-EIO` | `errno=EIO` | 已提交状态保持可解释，不向用户态暴露内部 block status |
| 运行期 backend 未初始化 | `Status::NotInitialized` | `-ENODEV` | `errno=ENODEV` | 不发布 fd 或目录变更 |
| 不支持的对象/backend 操作 | `Status::Unsupported` / `FsStatus::Unsupported` | `-EOPNOTSUPP` | `errno=EOPNOTSUPP` | 不修改目标对象 |
| 不可阻塞上下文 | syscall `!sched::can_block()` | `-EWOULDBLOCK` | `errno=EWOULDBLOCK` | 不进入 VFS/backend，不发布 fd、目录项、offset 或同步 IO |

## User-Visible Boundary

- libc syscall wrappers 位于 `user/libc/syscall.c`，统一使用 `errno_translate()`；用户态不读取 `vfs::Status`、`bigfs::Status` 或 `FsStatus` 名称。
- shell 位于 `user/sh/sh.c`，错误输出使用固定文案，例如 `sh: cannot open`、`sh: cd failed`、`sh: command not found`，不暴露 kernel/backend debug string。
- 小工具 `cat`、`ls`、`mkdir`、`rm`、`rename`、`stat` 输出 `errno=<number>`，依赖 libc `errno` 数值，不依赖内部 enum 名称。
- `status_name()` 只允许 kernel smoke、early boot/user ELF diagnostic 或 source-level diagnostics 使用；它不是用户 ABI。

## Blocking Context Review

以下路径可能分配、等待、触发同步块 IO、装入/落盘缓存或修改目录项，必须在进入 VFS/backend 前检查 `sched::can_block()`：

| syscall | guard | 后续副作用 |
| --- | --- | --- |
| `SYS_OPEN` | 是 | VFS init、路径解析、backend open/create/truncate、fd install |
| `SYS_READ` | 是 | fd read、pipe wait、block/cache read、用户缓冲复制 |
| `SYS_WRITE` | 是；console fd fast path 单独验证用户缓冲 | pipe/file write、缓存 dirty、fd offset 推进 |
| `SYS_PIPE` | 是 | 分配 pipe file object、安装 fd |
| `SYS_FSYNC` | 是 | writable backend cache flush |
| `SYS_MKDIR` | 是 | `/rw` inode/目录项分配与提交 |
| `SYS_UNLINK` | 是 | `/rw` 目录项删除和 open-ref 生命周期处理 |
| `SYS_RENAME` | 是 | `/rw` restricted regular-file rename |
| `SYS_READDIR` | 是 | 目录枚举、缓存读取、offset 推进 |
| `SYS_STAT` / `SYS_FSTAT` | 是 | metadata 查询、路径解析、用户输出复制 |
| `SYS_CHDIR` | 是 | 路径打开/metadata 验证后提交 cwd |
| `SYS_EXECVE` | 是 | bounded argv/envp 复制、ELF/VFS 读取、fd close-on-exec |

`SYS_CLOSE`、`SYS_LSEEK`、`SYS_DUP`、`SYS_DUP2`、`SYS_GETCWD` 当前不进入同步块 IO 或目录变更路径；它们仍通过 fd table、bounds 和用户缓冲校验保持确定性失败。

## Backend Dispatch And Open References

- 所有 cwd-relative fd/VFS 路径操作先经 `vfs::resolve_path(path, cwd, ...)` 归一化为 bounded absolute path，然后再进入 `open_absolute()`、`stat_absolute()`、`mkdir()`、`unlink()` 或 `rename()`；因此 absolute 与 cwd-relative 解析到同一目标时使用同一后端分派规则。
- `/rw` 由 `bigfs::owns_path()` 判定并路由到 RAM-backed writable backend；只读 exFAT 路径只允许 read/stat/readdir。对 exFAT 的 write/create/truncate/mkdir/unlink/rename 请求在进入后端修改前返回 `Status::ReadOnlyFs` / `-EROFS`。
- 跨后端 rename 或需要同一可写后端提交的目录变更，在 VFS 分派层检查失败；该失败发生在 `bigfs::rename()`、目录项修改、fd install、cwd commit 或 open file 引用修改之前。
- `dup()` / `dup2()` 保留同一 `vfs::File` 并调用 `vfs::retain()`，因此共享 open file offset；独立 `open()` 分配新的 `vfs::File`，因此 offset 独立。
- `fork()` 复制 fd table entry 并对每个 live file 执行 `retain()`，子进程继承同一 open file 引用；`execve()` 只关闭 close-on-exec fd，普通 fd 继续引用同一 `vfs::File`。
- `unlink()` 将路径可见性与 open file 生命周期分离：目录项删除后路径查找失败，但 `g_open_refs` 非零时 inode 保持可由已打开 fd 读取/stat，最后 close 后释放。
- restricted regular-file `rename()` 不替换已存在目标；目标已存在且非 no-op 时返回 `-EEXIST`，保持源、目标和 open file 引用不变。
- process exit/reap 的 `close_all_fds()` 对每个 live fd 执行 `vfs::release()`，确保 open-unlink 对象在最后引用释放后回收。

## Metadata Contract Review

- path metadata 通过 `vfs::stat_path()` 先解析 cwd-relative path，再进入 `stat_absolute()`；fd metadata 通过 `vfs::stat(file, out)` 读取 live open file 引用。
- exFAT metadata 使用 read-only mount state 填充 bounded `type/size/mode/uid/gid`，mode 固定为只读 file/dir 默认值，不分配 `/rw` 对象或 dirty exFAT cache。
- `/rw` metadata 通过 `bigfs::stat(inode, ...)` 读取已提交 inode 状态；成功 create/write/truncate/mkdir/unlink/rename 后，path lookup、fd stat 和最小目录枚举观察同一运行期状态。
- unlink 后路径 metadata 查询应返回 `-ENOENT`，但已打开 fd 的 metadata 继续通过 open file inode 返回，直到最后引用关闭。
- rename 后新路径 metadata 查询有效、旧路径 metadata 查询返回 `-ENOENT`，rename 前打开的 fd 继续有效。
- metadata syscall 先验证用户输出缓冲，再用 kernel-local `Metadata metadata = {}` 接收 VFS 结果；只有 VFS 成功后才复制到用户缓冲，因此失败不会发布 partial/uninitialized metadata。
- metadata 查询不调用 `read()`、`write()` 或 `lseek()`，因此失败不会推进 open file offset，也不会修改 cwd、fd table、目录项或 cache dirty state。
- `fill_metadata_defaults()` 将 `object_id` 置 0，保留字段为 0；当前 metadata contract 不承诺稳定 inode identity、完整 timestamp、ACL、xattr、device node、symlink 或跨重启持久化。

## Non-Goals

- 不承诺 `/rw` 跨重启持久化、journaling、磁盘分区承载或 exFAT 写入。
- 不暴露 stable inode identity、完整 timestamp、ACL、xattr、device node、symlink、完整 POSIX `DIR*` 或 atomic replacement。
- 不引入 mount namespace、async IO、SMP 文件系统并发语义或完整 POSIX permission/search-bit 模型。
