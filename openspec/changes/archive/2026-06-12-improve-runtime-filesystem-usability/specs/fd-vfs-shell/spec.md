## ADDED Requirements

### Requirement: fd/VFS 暴露有界运行时文件操作

BigOS SHALL 通过 fd/VFS shell 暴露有界运行时文件操作，使普通进程上下文中的 `open`、`read`、`write`、`lseek`、`fsync`、`mkdir`、最小目录枚举和 `unlink` 能路由到只读 exFAT 或 RAM-backed 可写后端。VFS MUST 保持只读 exFAT 拒写、`/rw` 可写、路径解析有界、错误码确定和 open file 引用生命周期稳定，MUST NOT 引入 mount namespace、异步 IO 或完整 POSIX VFS 语义。

#### Scenario: VFS 根据后端执行文件操作
- **WHEN** 进程通过 fd/VFS 对只读 exFAT 路径或 `/rw` 路径执行文件操作
- **THEN** 只读后端 MUST 对写/创建/删除返回拒写错误
- **AND** `/rw` 后端 MUST 在权限、容量和路径约束满足时执行对应有界操作

#### Scenario: VFS 操作只能在可阻塞上下文运行
- **WHEN** fd/VFS 文件操作需要分配、等待或同步块 IO
- **THEN** BigOS MUST 确认当前处于允许阻塞的进程上下文
- **AND** 在 IRQ、调度临界区或 preemption-disabled 不可阻塞路径中 MUST 确定性拒绝或进入文档化诊断路径

### Requirement: open file 引用和 offset 组合稳定

BigOS SHALL 让 VFS open file object 在 fd 复制、继承、关闭、删除路径和进程回收中具有稳定引用生命周期。dup/dup2 后 fd MUST 指向同一 open file object 并共享 offset；独立 open MUST 生成独立 offset；关闭、exec close-on-exec 和进程 reap MUST 精确释放引用一次。

#### Scenario: dup 共享 offset 而独立 open 不共享
- **WHEN** 进程 dup 一个文件 fd 并通过原 fd 写入或 seek
- **THEN** dup 后 fd MUST 观察同一 open file offset
- **AND** 同一路径的独立 open fd MUST 保持独立 offset

#### Scenario: unlink 后引用归零再回收
- **WHEN** 文件目录项已被 unlink 但仍有 open file 引用
- **THEN** VFS MUST 让新的路径查找不可见该目录项，并延迟释放仍被引用的文件对象、inode 和数据块，直到最后一个 open fd 引用关闭
- **AND** 进程退出或 reap MUST 关闭剩余 fd 并触发相同引用释放规则

### Requirement: fd/VFS 错误映射对用户态稳定

BigOS SHALL 将 VFS/backend 状态稳定映射为 syscall 层负 errno，包括 `ENOENT`、`EEXIST`、`EISDIR`、`ENOTDIR`、`EACCES`、`EROFS`、`ENOSPC`、`ENOMEM`、`EIO`、`EBADF`、`EINVAL` 和 `ESPIPE` 等已支持错误。错误映射 MUST 对简单 C 程序和 shell 重定向可观察，MUST NOT 依赖源码内部枚举名。

#### Scenario: shell 重定向失败可报告
- **WHEN** shell 输出重定向因只读路径、权限拒绝或空间耗尽失败
- **THEN** fd/VFS 与 syscall 层 MUST 返回可被 libc 翻译的确定性 errno
- **AND** shell MUST 能报告错误并保持自身 fd 状态可用

#### Scenario: 非法 fd 和不可定位对象返回稳定错误
- **WHEN** 进程对非法 fd 执行 I/O，或对管道 fd 执行 `lseek`
- **THEN** BigOS MUST 分别返回确定性 bad-fd 或不可 seek 错误
- **AND** MUST NOT 访问已释放 open file state

### Requirement: fd/VFS 支持有界目录枚举对象

BigOS SHALL 让 fd/VFS 对目录 fd 提供最小目录枚举操作。目录 fd MUST 与普通文件 fd 一样受 fd table 引用生命周期管理；枚举操作 MUST 经后端返回有界目录项记录，至少包含名称和基础类型；对普通文件、管道、非法 fd 或不支持枚举的后端 MUST 返回确定性错误。该能力 MUST NOT 引入完整 POSIX `DIR*` 流、跨调用稳定快照或 mount namespace。

#### Scenario: 目录 fd 枚举返回有界记录
- **WHEN** 进程打开 `/rw` 目录并请求最小目录枚举
- **THEN** fd/VFS MUST 返回不超过调用方缓冲区和系统上限的目录项记录
- **AND** 每个记录 MUST 至少携带目录项名称和基础类型

#### Scenario: 非目录对象拒绝枚举
- **WHEN** 进程对普通文件 fd、管道 fd、非法 fd 或已关闭 fd 请求目录枚举
- **THEN** fd/VFS MUST 返回确定性错误
- **AND** MUST NOT 修改 open file offset 之外的无关 fd 状态
