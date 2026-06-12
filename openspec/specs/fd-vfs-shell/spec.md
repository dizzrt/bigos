## Purpose

定义 BigOS 最小 fd/VFS shell 能力：在现有只读 exFAT 和 block stack 之上提供
root vnode、只读绝对路径 `open`、open file `read`、进程 fd table、fd lifecycle
integration、blocking context boundaries, and reproducible validation. This
capability does not introduce writable filesystems, page cache, directory
mutation, multiple mount namespaces, broad POSIX semantics, or user-space libc.

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

BigOS SHALL support opening read-only regular files by absolute path through the VFS shell. The operation MUST accept only bounded, NUL-terminated absolute paths and read-only flags, and MUST reject unsupported path forms or write-capable flags deterministically.

#### Scenario: open 找到普通文件

- **WHEN** a caller opens an existing absolute path such as `/boot/fs_smoke.txt` with read-only flags
- **THEN** VFS MUST resolve the path through the exFAT backend and create an open file object with offset zero and read operations bound to that file

#### Scenario: open 拒绝不支持的请求

- **WHEN** a caller opens a relative path, an overlong path, a non-regular file, a missing path, or a path with write/create/truncate flags
- **THEN** VFS MUST fail with a deterministic error and MUST NOT allocate a published file descriptor or mutate filesystem state

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
