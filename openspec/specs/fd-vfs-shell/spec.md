## Purpose

定义 BigOS fd/VFS shell 能力：在现有只读 exFAT、RAM-backed `/rw` 可写后端和
block stack 之上提供 root vnode、路径型 `open`/`rename`、open file `read`/`write`、
进程 fd table、fd lifecycle integration、blocking context boundaries, and
reproducible validation. This capability remains bounded and does not introduce
multiple mount namespaces, symlink traversal, async I/O, broad POSIX semantics,
or a complete user-space libc.

## Requirements

### Requirement: 最小 VFS 挂载和 vnode 抽象

BigOS SHALL provide a minimal read-only VFS shell that exposes a root vnode backed by the existing exFAT filesystem implementation. The VFS shell MUST preserve the existing MBR/exFAT validation and bounded read behavior, and MUST NOT introduce writable filesystem mutation, page cache, directory mutation, or multiple mount namespaces.

#### Scenario: VFS 初始化挂载只读 exFAT root

- **WHEN** kernel initialization enables the fd/VFS capability after block device and memory prerequisites are available
- **THEN** the VFS layer MUST discover and mount the supported read-only exFAT volume through the existing block/filesystem stack
- **AND** it MUST expose a root vnode for later absolute path lookup without changing the raw image, MBR, exFAT, ATA PIO, or boot layout contracts

#### Scenario: VFS 初始化失败有确定性结果

- **WHEN** the underlying block device, MBR discovery, or exFAT mount fails
- **THEN** the VFS layer MUST return or record a deterministic error and MUST NOT publish a partially initialized root mount

### Requirement: 只读绝对路径 open

BigOS SHALL support opening read-only regular files by absolute path or by relative path resolved from the current process cwd through the VFS shell. The operation MUST accept only bounded, NUL-terminated paths and read-only flags for the read-only backend, MUST apply the same path resolution contract as other path-taking VFS operations including POSIX-style `.`/`..` components, and MUST reject unsupported path forms or write-capable flags deterministically.

#### Scenario: open 找到普通文件

- **WHEN** a caller opens an existing absolute path such as `/boot/fs_smoke.txt` with read-only flags
- **THEN** VFS MUST resolve the path through the exFAT backend and create an open file object with offset zero and read operations bound to that file

#### Scenario: open 从 cwd 解析相对路径

- **WHEN** 当前进程 cwd 指向一个支持的目录且调用方以 read-only flags 打开相对路径
- **THEN** VFS MUST 先按 cwd 合成有界目标路径，再通过对应后端创建 open file object
- **AND** 成功打开的 fd MUST 与同一目标绝对路径打开时具有相同读语义

#### Scenario: open 解析 dot-dot 组件

- **WHEN** 当前进程 cwd 为 `/rw/work/sub` 且调用方以 read-only flags 打开 `../note.txt`
- **THEN** VFS MUST resolve the target as `/rw/work/note.txt`
- **AND** the resulting open file behavior MUST match opening that absolute target directly

#### Scenario: open 拒绝不支持的请求

- **WHEN** a caller opens an overlong path, non-NUL-terminated path, non-regular file, missing path, unsupported relative path form, or a path with write/create/truncate flags on a read-only backend
- **THEN** VFS MUST fail with a deterministic error and MUST NOT allocate a published file descriptor or mutate filesystem state

### Requirement: fd/VFS 路径入口共享 cwd 解析

BigOS SHALL route path-taking fd/VFS operations through a shared bounded path resolution helper that understands both absolute paths and relative paths rooted at the current process cwd, including POSIX-style `.`/`..` component handling within the supported BigOS directory tree. This shared helper MUST be used consistently for open, writable runtime file operations, directory operations, and path-based dispatch where applicable. It MUST NOT introduce mount namespaces, `chroot`, symlink traversal, async I/O, or complete POSIX VFS semantics.

#### Scenario: 可写后端相对路径操作

- **WHEN** 当前进程 cwd 位于 `/rw` 下且进程创建、打开、写入、同步、枚举或删除相对路径
- **THEN** fd/VFS MUST route the operation to the RAM-backed writable backend for the cwd-resolved target
- **AND** backend permission, capacity, reference lifecycle, and deterministic errno behavior MUST remain unchanged

#### Scenario: 只读后端相对写失败

- **WHEN** 当前进程 cwd 位于只读 exFAT 路径下且进程以相对路径请求写入、创建或删除
- **THEN** fd/VFS MUST return the same deterministic read-only or unsupported error as the equivalent absolute target
- **AND** MUST NOT modify filesystem state or publish a partially initialized fd

#### Scenario: 不可阻塞上下文拒绝路径解析副作用

- **WHEN** cwd-based path operation would require allocation, backend lookup, blocking disk I/O, or wait operations from IRQ, scheduler critical section, or preemption-disabled path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** MUST NOT perform blocking I/O or unbounded allocation from that context

### Requirement: open file read 语义

BigOS SHALL provide bounded reads from VFS open file objects. Reads MUST use the file object's current offset, copy at most the requested byte count, clamp at end-of-file, advance offset only by bytes successfully read, and validate all offset arithmetic for overflow.

#### Scenario: read 成功推进 offset

- **WHEN** a caller reads from an open regular file into a valid destination buffer
- **THEN** VFS MUST read bytes through the exFAT backend starting at the current file offset
- **AND** it MUST return the actual byte count and advance the file offset by that count

#### Scenario: read 到 EOF

- **WHEN** a caller reads from an open file whose current offset is at or beyond file size
- **THEN** VFS MUST return success with zero bytes read and MUST NOT issue out-of-bounds backend reads

#### Scenario: read 失败不推进 offset

- **WHEN** backend read, destination validation, allocation, or block IO fails before bytes are successfully delivered
- **THEN** VFS MUST return a deterministic error and MUST NOT advance the open file offset for failed bytes

### Requirement: 进程 fd table

BigOS SHALL provide each process with a file descriptor table. 该 fd table SHALL 为可增长结构，其容量由可配置软上限约束，而非编译期固定的 `MAX_FDS` 硬上限，并随进程对象生命周期分配与回收 fd 存储。The table MUST allocate descriptors deterministically, prefer the lowest available descriptor, map descriptors to open file objects, reject invalid descriptors, and release references when descriptors are closed or the process is reaped.

#### Scenario: open 分配最低可用 fd

- **WHEN** a process opens a file and its fd table is below its soft limit
- **THEN** BigOS MUST allocate a stable nonnegative fd from the process table, prefer the lowest available descriptor, and bind it to the open file object
- **AND** allocation MUST succeed even when the number of open descriptors exceeds the former fixed bound (16) while below the soft limit

#### Scenario: fd table 容量耗尽

- **WHEN** a process opens a file but allocation would exceed the configured fd soft limit, or growing the fd storage requires a kernel-heap allocation that fails
- **THEN** BigOS MUST fail open deterministically with `EMFILE`, MUST release any unpublished open file object, and MUST NOT panic

#### Scenario: close 释放 fd

- **WHEN** a process closes a valid fd
- **THEN** BigOS MUST remove that fd table entry and drop the open file reference exactly once

#### Scenario: bad fd 被拒绝

- **WHEN** a process reads or closes an fd that is unused, already closed, outside current table bounds, or not readable
- **THEN** BigOS MUST return a deterministic bad-fd error and MUST NOT access freed file state

### Requirement: fd 生命周期和 exec 继承

BigOS SHALL integrate fd table ownership with process lifecycle. Process creation MUST initialize an fd table, `exec` MUST apply explicit fd inheritance or close-on-exec rules, and process exit/reap MUST close all remaining open files from a safe kernel context.

#### Scenario: process 创建初始化 fd table

- **WHEN** a process object is created and published
- **THEN** BigOS MUST initialize its fd table to an empty or explicitly seeded state without requiring a smoke-only user program configuration

#### Scenario: exec 继承 fd

- **WHEN** a process successfully commits a new image through `exec`
- **THEN** BigOS MUST preserve fd table entries that are not marked close-on-exec and MUST close entries marked close-on-exec before entering the new user image

#### Scenario: exit 或 reap 关闭 fd

- **WHEN** a process exits, faults, or reaches the safe reaper boundary
- **THEN** BigOS MUST close all remaining fd table entries exactly once and MUST NOT free active file state from an unsafe syscall, exception, IRQ, or active-stack teardown path

### Requirement: fd/VFS 阻塞上下文边界

BigOS SHALL run fd/VFS open and read operations only in contexts where blocking, allocation, and synchronous block IO are allowed. The VFS and syscall layers MUST reject or diagnose calls from IRQ context, scheduler critical sections, preemption-disabled nonblocking regions, or other contexts that must not block.

#### Scenario: 可阻塞进程上下文执行 I/O

- **WHEN** a user process invokes `open` or `read` from a normal syscall context where blocking is permitted
- **THEN** BigOS MAY allocate kernel objects and perform synchronous exFAT/ATA PIO reads according to the VFS contract

#### Scenario: 不可阻塞上下文拒绝 I/O

- **WHEN** fd/VFS open or read is invoked from IRQ, exception-only diagnostic paths, preemption-disabled scheduler critical sections, or another nonblocking context
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT enqueue a wait state, perform unbounded allocation, or issue blocking disk IO from that context

### Requirement: fd/VFS 验证可复现

BigOS SHALL provide reproducible validation for the fd/VFS shell, including source-level checks and default-off runtime smoke coverage. Validation MUST record toolchain and emulator availability, serial markers, skipped cases, and residual bootability risk.

#### Scenario: source checks 覆盖 fd/VFS 不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover VFS root initialization, open success and failure, fd allocation capacity, bad-fd handling, read EOF clamp, offset advancement, close idempotence rejection, exec inheritance, and exit/reap close-all behavior

#### Scenario: runtime smoke 记录 marker

- **WHEN** fd/VFS runtime smoke is enabled
- **THEN** BigOS MUST validate at least one read-only file through open/read/close and emit deterministic COM1 markers for pass or fail outcomes
- **AND** unavailable QEMU, Bochs, cross-binutils, ROM/display, raw-image, serial oracle, or timeout dependencies MUST be recorded as skipped validation rather than claimed as passed

### Requirement: VFS 可写 open 与写/lseek 文件操作

BigOS SHALL 扩展 fd/VFS 壳层以支持可写文件操作：`FileOperations` MUST 新增 `write` 与 `lseek` 操作（追加，不破坏既有 `read`/`close` 与 `File` 既有字段布局），`open_absolute` MUST 接受可写/创建 flags 与 `O_CREAT` 时的 mode/owner 入参，`vfs::Status` MUST 覆盖只读后端拒写、无空间与权限拒绝等失败。只读后端的 `write` MUST 返回拒写错误，且现有只读 open/read/close 与只读 exFAT 行为 MUST 保持不变。

#### Scenario: 可写 open 创建带 owner/mode 的文件

- **WHEN** 调用方以可写/创建 flags 与 mode 通过 `open_absolute` 打开可写后端的路径
- **THEN** VFS MUST 经可写后端创建/打开可写文件对象，记录 owner 为调用进程身份、采用传入 mode，并将该文件对象标记为可写

#### Scenario: 文件 write 经后端写入并推进 offset

- **WHEN** 调用方对可写打开文件对象调用 VFS `write`
- **THEN** VFS MUST 经可写后端与块缓冲缓存写入数据、按成功字节推进 offset，失败时不推进 offset 并返回确定性错误

#### Scenario: lseek 校验溢出与不可定位对象

- **WHEN** 调用方对打开文件对象调用 VFS `lseek`
- **THEN** VFS MUST 对可定位文件返回校验过溢出的新 offset，对管道等不可定位对象返回 `-ESPIPE`，对非法 whence/溢出返回 `-EINVAL`

#### Scenario: 只读后端拒写

- **WHEN** 调用方对只读 exFAT 后端的文件对象调用 VFS `write` 或以写 flags 打开
- **THEN** VFS MUST 返回 `-EROFS` 并将其映射为对应 `vfs::Status`，MUST NOT 修改文件系统状态

### Requirement: fd 表支持管道与 dup/dup2

BigOS SHALL 让进程 fd 表支持指向管道端的打开文件对象与 `dup`/`dup2` 复制。`dup`/`dup2` 复制的 fd MUST 指向同一打开文件对象并共享 offset 与底层引用计数，`dup2` MUST 在目标 fd 已打开时先关闭它，引用计数 MUST 保证每个 fd 关闭时底层对象引用精确递减一次。

#### Scenario: dup 共享同一打开文件对象

- **WHEN** 进程对一个有效 fd 调用 `dup`/`dup2`
- **THEN** fd 表 MUST 把新 fd 绑定到同一打开文件对象、增加其引用计数，并令两个 fd 共享同一 offset

#### Scenario: 关闭共享 fd 精确递减引用

- **WHEN** 多个 fd 共享同一打开文件对象，其中一个 fd 被 close
- **THEN** fd 表 MUST 仅移除该 fd 项并把底层对象引用递减一次，仅在引用归零时释放底层对象

#### Scenario: 管道端 fd 纳入 exec 继承与退出关闭

- **WHEN** 进程 `exec` 或退出/被回收，且其 fd 表含管道端 fd
- **THEN** fd 表 MUST 按 close-on-exec 规则在 exec 时关闭或保留管道端 fd，并在退出/回收时关闭所有剩余管道端 fd 各一次

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

### Requirement: fd/VFS 运行时错误映射稳定

BigOS SHALL make fd/VFS path-taking and fd-taking runtime filesystem operations return stable user-visible negative errno values across VFS dispatch, backend failures, syscall return paths, libc wrappers, and shell-visible errors. The mapping MUST cover missing paths, existing targets, invalid object types, read-only backend writes, permission denial, capacity exhaustion, invalid descriptors, illegal user buffers, unsupported seek/enumeration targets, caller output buffer exhaustion, backend IO failures, and nonblocking-context rejection. Directory enumeration MUST map caller output capacity exhaustion to `-ERANGE`, illegal enumeration arguments to `-EINVAL`, and backend capacity exhaustion to `-ENOSPC` or the documented backend capacity error. The mapping MUST NOT expose internal backend enum names as user ABI.

#### Scenario: 只读后端写请求返回稳定错误

- **WHEN** 用户进程对只读 exFAT 路径发起创建、写入、截断、删除或 rename 请求
- **THEN** fd/VFS MUST 返回稳定的 read-only filesystem 错误
- **AND** syscall/libc/shell 观察到的错误 MUST 与等价只读后端操作一致
- **AND** fd/VFS MUST NOT 修改只读 filesystem、raw image、boot assets、fd table 或 cwd 状态

#### Scenario: 不同失败点映射为确定性 errno

- **WHEN** 文件操作在路径解析、fd 查找、用户缓冲校验、权限检查、容量检查、对象类型检查或 backend IO 阶段失败
- **THEN** fd/VFS MUST 返回对应确定性负 errno
- **AND** 失败路径 MUST NOT 依赖未初始化输出、内部枚举值或调试字符串供用户态判定

#### Scenario: 目录枚举缓冲不足返回 ERANGE

- **WHEN** 用户进程对有效目录 fd 执行最小目录枚举，但调用方提供的输出容量不足以容纳需要返回的有界目录项结果
- **THEN** fd/VFS MUST return `-ERANGE`
- **AND** MUST NOT report the condition as backend space exhaustion
- **AND** MUST NOT modify directory state, unrelated fd state, or publish partial uninitialized directory entries

#### Scenario: 不可阻塞上下文拒绝文件系统副作用

- **WHEN** fd/VFS 操作会执行分配、等待、同步块 IO、缓存落盘或目录项变更，但调用上下文为 IRQ、调度临界区、preemption-disabled 区域或其它不可阻塞路径
- **THEN** BigOS MUST 确定性失败或进入文档化诊断路径
- **AND** MUST NOT 发布 fd、修改目录项、推进 offset、执行阻塞 IO 或等待队列操作

### Requirement: fd/VFS 后端分派差异可观察

BigOS SHALL dispatch runtime filesystem operations according to the resolved backend and object type, preserving the difference between read-only exFAT and RAM-backed writable `/rw`. The same cwd-relative or absolute path operation MUST produce equivalent behavior after resolving to the same backend target. Backend dispatch MUST remain bounded and MUST NOT introduce mount namespaces, symlink traversal, broad file-backed mappings, async IO, or full POSIX VFS semantics.

#### Scenario: cwd 相对路径保持后端差异

- **WHEN** 当前进程 cwd 位于只读 exFAT 路径或 `/rw` 路径下，并以相对路径执行 open、write、mkdir、unlink、rename、stat 或目录枚举
- **THEN** fd/VFS MUST 先按共享路径解析契约得到目标
- **AND** MUST 按解析后的后端应用只读拒写或 `/rw` 可写语义

#### Scenario: 跨后端目录变更被拒绝

- **WHEN** 用户进程尝试在只读 exFAT 与 `/rw` 之间执行 rename 或其它需要同一可写后端提交的目录变更
- **THEN** fd/VFS MUST 返回稳定的跨后端或不支持错误
- **AND** MUST NOT 修改任一后端状态、已打开 fd、cwd、缓存块或目录枚举结果

### Requirement: fd/VFS 运行时行为验证覆盖组合路径

BigOS SHALL provide behavior-oriented validation for fd/VFS runtime filesystem semantics across simple user programs and shell-visible operations. Validation MUST cover success and failure paths for read-only exFAT, RAM-backed `/rw`, path and fd operations, directory object handling, open file references, and deterministic errno reporting. Environment-dependent emulator checks MUST record missing QEMU, Bochs, cross toolchain, ROM/display, raw image, serial oracle, or timeout dependencies as skipped rather than passed.

#### Scenario: 用户态观察成功和失败路径

- **WHEN** runtime filesystem semantics validation runs with required build and emulator support
- **THEN** it MUST exercise at least one read-only file success path, one `/rw` create/write/read/stat/list path, and representative failure paths for read-only write, missing path, invalid fd, permission denial, and illegal object type
- **AND** the observed errno and file state MUST match the fd/VFS contract

#### Scenario: 环境不可用时记录跳过

- **WHEN** emulator, cross toolchain, disk image, display/ROM, serial oracle, or timeout dependencies are unavailable
- **THEN** validation notes MUST record the unavailable dependency and residual risk
- **AND** they MUST NOT claim fd/VFS runtime semantics validation passed
