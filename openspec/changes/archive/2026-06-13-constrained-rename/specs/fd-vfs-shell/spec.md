## ADDED Requirements

### Requirement: fd/VFS 暴露受限路径 rename

BigOS SHALL 通过 fd/VFS shell 暴露受限路径 `rename` 操作，使普通进程上下文中的源路径和目标路径经同一 cwd/相对路径解析、后端分派、权限检查、阻塞上下文守卫和错误映射边界处理。VFS MUST 只把同一 `/rw` 可写后端内的常规文件 rename 委派给后端，MUST 对只读后端、跨挂载、目录对象、不支持对象、非法路径和不可阻塞上下文确定性失败，且 MUST NOT 引入 mount namespace、symlink traversal、async I/O 或完整 POSIX VFS 语义。

#### Scenario: VFS 解析相对路径 rename

- **WHEN** 当前进程 cwd 位于 `/rw/work` 且调用方把 `old.txt` rename 为 `sub/new.txt`
- **THEN** fd/VFS MUST 按当前 cwd 解析源路径和目标路径
- **AND** 在两者属于同一可写后端且约束满足时 MUST 委派后端完成目录项变更

#### Scenario: VFS 同名同目录 rename 为 no-op

- **WHEN** fd/VFS 解析 rename 源路径和目标路径后发现二者指向同一父目录下的同一名称
- **THEN** fd/VFS MUST 返回成功
- **AND** MUST NOT 委派会产生目录项副作用的后端更新

#### Scenario: 跨后端 rename 被拒绝

- **WHEN** 调用方尝试把只读 boot asset 路径 rename 到 `/rw`，或把 `/rw` 路径 rename 到只读后端
- **THEN** fd/VFS MUST 返回确定性错误
- **AND** MUST NOT 修改任一后端状态或发布半成品目录项

#### Scenario: rename 只能在可阻塞进程上下文运行

- **WHEN** rename 路径需要分配、等待、同步块 IO 或目录项更新
- **THEN** fd/VFS MUST 确认当前处于允许阻塞的普通进程上下文
- **AND** 在 IRQ、调度临界区或 preemption-disabled 不可阻塞路径中 MUST 确定性拒绝或进入文档化诊断路径

### Requirement: rename 错误映射对用户态稳定

BigOS SHALL 将 VFS/backend rename 失败稳定映射为 syscall 层负 errno，包括 `ENOENT`、`EEXIST`、`ENOTDIR`、`EISDIR`、`EACCES`、`EROFS`、`EXDEV` 或 BigOS 文档化的跨后端错误、`ENOSPC`、`ENOMEM`、`EIO`、`EINVAL` 和 `EFAULT` 等已支持或随本能力补齐的错误。错误映射 MUST 对简单 C 程序和 shell 工具可观察，MUST NOT 依赖源码内部枚举名。

#### Scenario: 用户态观察 rename 失败 errno

- **WHEN** 用户态 rename 因源缺失、目标已存在、只读后端、跨后端、权限拒绝或非法路径失败
- **THEN** syscall 返回值 MUST 为对应确定性负 errno
- **AND** libc wrapper 和用户态工具 MUST 能把错误报告给调用方或 shell

#### Scenario: 目标已存在映射为稳定错误

- **WHEN** rename 的目标路径已存在且源目标不是同一父目录同一名称
- **THEN** fd/VFS MUST 将该失败稳定映射为 `-EEXIST` 或 BigOS 文档化的目标已存在错误
- **AND** MUST NOT 修改源对象、目标对象或无关 fd/VFS 状态

#### Scenario: rename 失败不破坏 fd/VFS 状态

- **WHEN** rename 在路径解析、后端检查或提交前失败
- **THEN** fd/VFS MUST 保持当前进程 fd table、cwd、已打开文件对象和无关目录项可用
- **AND** 后续 open/read/write/list 操作 MUST 仍可按既有契约运行
