## Purpose

定义 BigOS 最小可写文件系统能力：与现有只读 exFAT 挂载并存的可写后端，支持
`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC` 打开、文件 `write` 与 `lseek`、文件创建/截断、
目录项 `mkdir`/`unlink` 和受限常规文件 `rename` 的最小子集，inode 携带 owner/mode 元数据并以 `cred::may_access`
为实际访问强制点，所有写经块缓冲缓存，只读后端对写请求确定性 `-EROFS`。该能力本阶段默认
承载介质为 RAM-backed 块设备、只保证运行期一致性而不承诺跨重启持久化，不引入完整 POSIX
文件语义（无硬/软链接、无完整目录 rename 或 POSIX atomic replacement、无 mmap 文件映射、无 ACL/xattr），并以默认关闭的运行时
smoke 验证。

## Requirements

### Requirement: 可写文件系统后端与只读 exFAT 并存

BigOS SHALL 提供一个最小可写文件系统后端，与现有只读 exFAT 挂载并存且不改变只读 exFAT 的发现、挂载与读语义。可写后端 MUST 经块缓冲缓存读写其超级块、inode、目录项与数据块，MUST 维护文件 inode 的 owner（uid/gid）与 mode 元数据，MUST 有界（块大小、inode 数、文件大小、目录项数均有上限），且 MUST NOT 引入硬/软链接、完整目录 rename、POSIX atomic replacement、journaling、ACL/xattr 或文件 mmap。本阶段可写后端的默认承载介质 MUST 为 RAM-backed 块设备（不改动现有磁盘镜像/MBR/分区/exFAT 只读发现契约），其正确性语义只覆盖运行期一致性，MUST NOT 承诺跨重启持久化；磁盘分区承载 MUST 不在本阶段范围。

#### Scenario: 可写后端挂载且不影响只读 exFAT

- **WHEN** 内核初始化在块设备与缓存就绪后启用可写文件系统后端
- **THEN** 可写后端 MUST 在其挂载点（基于默认 RAM-backed 块设备）暴露可写根，且现有只读 exFAT 的挂载、路径查找与只读读 MUST 保持不变

#### Scenario: 只读后端拒绝写请求

- **WHEN** 调用方对只读 exFAT 后端的文件发起写、创建、截断或删除
- **THEN** BigOS MUST 返回确定性 `-EROFS`，MUST NOT 修改任何文件系统状态

#### Scenario: 承载介质初始化失败确定性降级

- **WHEN** 可写后端的 RAM-backed 承载介质分配失败
- **THEN** BigOS MUST 确定性地不发布可写挂载，MUST 保持只读 exFAT 路径不受影响，MUST NOT panic

### Requirement: 可写打开与文件创建

BigOS SHALL 支持以可写/创建 flags（`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`）按绝对路径打开可写后端的常规文件。`O_CREAT` 创建的新文件 MUST 记录调用进程身份为 owner 并采用调用方提供的 mode；打开/创建 MUST 在执行前经访问权限判定，并对非法路径、空间耗尽与权限拒绝确定性失败。

#### Scenario: O_CREAT 创建新文件

- **WHEN** 调用方以 `O_CREAT` 打开一个不存在的合法绝对路径且其目录可写、空间充足
- **THEN** BigOS MUST 在可写后端创建该文件、记录 owner 为调用进程 uid/gid、采用调用方 mode，并返回一个可写打开文件对象，offset 为 0

#### Scenario: O_TRUNC 截断已有文件

- **WHEN** 调用方以可写 flags 与 `O_TRUNC` 打开一个已存在且有写权限的文件
- **THEN** BigOS MUST 把文件长度截断为 0 并释放其多余数据块，offset 为 0

#### Scenario: 无空间创建失败

- **WHEN** 创建文件需要分配 inode 或数据块但位图已耗尽
- **THEN** BigOS MUST 返回确定性 `-ENOSPC`，MUST NOT 发布半成品 inode 或目录项

### Requirement: 文件写与 lseek

BigOS SHALL 提供从打开文件对象当前 offset 的有界写入与 `lseek` 定位。写 MUST 经块缓冲缓存写入并标脏、按成功写入字节推进 offset、对越界/无空间/IO 失败前不推进 offset，并校验所有 offset 算术溢出；`lseek` MUST 校验溢出，且对不可定位对象（如管道）返回确定性错误。

#### Scenario: write 推进 offset 且对读可见

- **WHEN** 调用方向一个可写打开文件写入合法用户缓冲
- **THEN** BigOS MUST 经缓存写入数据、按实际写入字节推进 offset，且后续对同一文件的读 MUST 看到写入内容

#### Scenario: write 失败不推进 offset

- **WHEN** 写入因无空间、IO 错误或用户缓冲校验失败而未成功交付字节
- **THEN** BigOS MUST 返回确定性错误，MUST NOT 推进 offset，MUST NOT 破坏已有数据

#### Scenario: lseek 定位与溢出校验

- **WHEN** 调用方对可定位文件以合法 whence 与 offset 调用 `lseek`
- **THEN** BigOS MUST 返回新的绝对 offset；当 offset 算术溢出或 whence 非法时 MUST 返回 `-EINVAL`

#### Scenario: 对管道 lseek 被拒绝

- **WHEN** 调用方对管道端文件对象调用 `lseek`
- **THEN** BigOS MUST 返回确定性 `-ESPIPE`

### Requirement: 目录项创建与删除

BigOS SHALL 在可写后端支持最小目录项创建（`mkdir`）、删除（`unlink`）与受限常规文件 `rename`。操作 MUST 在执行前经访问权限判定，对已存在/不存在、目录非空、对目录 `unlink`、空间耗尽与只读后端确定性失败，且 MUST NOT 引入完整 `readdir`/`getdents` 遍历、完整目录 rename 或 POSIX atomic replacement。

#### Scenario: mkdir 创建目录

- **WHEN** 调用方对一个不存在的合法路径在可写、空间充足的父目录下 `mkdir`
- **THEN** BigOS MUST 创建目录项与目录 inode、记录 owner 与 mode，并返回成功

#### Scenario: mkdir 已存在被拒绝

- **WHEN** 调用方 `mkdir` 的路径已存在
- **THEN** BigOS MUST 返回确定性 `-EEXIST`，MUST NOT 修改文件系统状态

#### Scenario: unlink 删除常规文件

- **WHEN** 调用方对一个存在且有权限的常规文件 `unlink`
- **THEN** BigOS MUST 移除其目录项，并在无其它引用时释放其 inode 与数据块

#### Scenario: unlink 非法目标被拒绝

- **WHEN** 调用方 `unlink` 一个不存在的路径，或对目录调用 `unlink`
- **THEN** BigOS MUST 分别返回确定性 `-ENOENT` 或 `-EISDIR`，MUST NOT 修改文件系统状态

### Requirement: 写一致性与确定性失败语义

BigOS SHALL 以写回语义保证写后读回一致：写经缓存即对后续读可见，落盘发生在 `fsync`、缓存淘汰回写或全量同步时。可写后端的所有失败 MUST 以确定性错误返回，MUST NOT 在失败路径破坏已落盘元数据的一致性，MUST NOT panic（内核态 fault 除外）。

#### Scenario: write 经缓存即对读可见，fsync 落盘

- **WHEN** 调用方写入文件后未 `fsync` 即读回
- **THEN** BigOS MUST 返回写入后的内容（页缓存可见性）
- **AND** 调用 `fsync` 后内容 MUST 已落盘，且落盘后即便缓存淘汰再读仍一致

#### Scenario: 元数据更新失败不留半成品

- **WHEN** 创建/截断/删除在提交 inode、位图或目录项的过程中因空间或 IO 失败而中止
- **THEN** BigOS MUST 不发布半成品元数据、以确定性错误返回，并保持先前一致状态

### Requirement: 可写文件系统验证可复现

BigOS SHALL 通过默认关闭的运行时 smoke 与源码/行为断言验证可写文件系统。验证 MUST 记录工具链与模拟器可用性、串口 marker、跳过的用例与残余风险，且默认启动 marker 与既有 smoke 矩阵 MUST 保持不变。

#### Scenario: 可写 FS smoke 发射有界 marker

- **WHEN** 启用 `writable_fs_smoke`（`BIGOS_WRITABLE_FS_SMOKE`）构建并在模拟器中启动
- **THEN** 验证 MUST 覆盖「`O_CREAT` 建文件 + 写 + 读回一致」「`fsync` 落盘后淘汰再读一致」「owner/mode 权限拒绝」「只读后端写被 `EROFS` 拒绝」并发射 `BIGOS_WRITABLE_FS_PASSED`/`BIGOS_WRITABLE_FS_FAILED`
- **AND** 该开关 MUST 默认关闭；QEMU/Bochs、交叉工具链或磁盘镜像不可用时 MUST 记录为跳过验证而非声称通过

### Requirement: 运行时文件系统行为可被简单 C 程序依赖

BigOS SHALL 将 RAM-backed 可写后端定义为有界运行时文件系统能力，使简单静态 C 程序能够在 `/rw` 范围内可靠使用文件创建、打开、读取、写入、定位、同步、目录创建、最小目录枚举、删除和受限常规文件 rename。该能力 MUST 保持运行期一致性，MUST 对路径长度、文件大小、目录项、inode、数据块和打开文件引用设置有界限制，MUST NOT 承诺跨重启持久化、磁盘分区承载、journaling、完整目录 rename、硬/软链接、ACL/xattr、完整目录遍历、完整 POSIX `readdir/getdents` 兼容或 broad file-backed `mmap`。

#### Scenario: 简单程序创建并读回文件
- **WHEN** 简单 C 程序在 `/rw` 下创建文件、写入有界内容、seek 回起点并读取
- **THEN** BigOS MUST 返回写入内容，offset、返回字节数和 EOF 行为 MUST 确定且与 fd/VFS 语义一致

#### Scenario: 运行期边界不等于持久化
- **WHEN** 文档、规格或验证描述 `/rw` 可写行为
- **THEN** 它们 MUST 明确该行为只保证当前运行期一致性
- **AND** MUST NOT 暗示重启后数据仍存在或现有磁盘镜像布局被修改

### Requirement: 运行时文件状态跨 fd 和进程组合可见

BigOS SHALL 保证同一运行期内经不同 fd、dup 后 fd、fork/exec 继承 fd 或重新打开路径访问同一可写文件时，已提交到缓存的写入对后续读可见。失败的写入、截断、创建、删除或同步 MUST NOT 发布半成品状态，MUST NOT 推进失败操作不应推进的 offset，MUST 返回确定性负 errno。

#### Scenario: 多 fd 观察同一写入
- **WHEN** 一个进程通过某 fd 写入文件并通过另一个指向同一文件的 fd 读取
- **THEN** 读取 MUST 观察到已成功写入的数据
- **AND** dup 后 fd MUST 共享同一 open file offset，重新打开路径的 fd MUST 使用独立 offset

#### Scenario: 失败操作不污染状态
- **WHEN** 文件操作因容量耗尽、权限拒绝、非法路径、非法用户缓冲或块 IO 错误失败
- **THEN** BigOS MUST 返回确定性错误
- **AND** 已存在文件数据、目录项、inode 元数据和相关 fd offset MUST 保持在失败前的可解释状态

### Requirement: 删除与目录变更具有有界引用语义

BigOS SHALL 支持有界 `mkdir` 与 `unlink` 行为。`unlink` 常规文件 MUST 先移除目录项，使新的路径查找不再找到该目录项；若仍有打开文件引用，文件对象 MUST 在引用归零前保持有效并可按打开权限继续访问，inode 与数据块 MUST 整体延迟到最后一个 open fd 关闭后再释放。目录删除、非空目录删除、只读后端删除和不支持的目录遍历 MUST 返回确定性错误。

#### Scenario: 删除已打开文件后 fd 仍有效
- **WHEN** 进程打开 `/rw` 文件后对其路径执行 `unlink`
- **THEN** 后续路径查找 MUST 返回不存在
- **AND** 已打开 fd MUST 在关闭前继续按其打开权限访问对应文件内容
- **AND** inode 与数据块 MUST 在最后一个 open fd 关闭后再释放

#### Scenario: 删除未打开文件立即回收
- **WHEN** 进程对一个没有 open fd 引用的 `/rw` 常规文件执行 `unlink`
- **THEN** BigOS MUST 移除其目录项并释放对应 inode 与数据块
- **AND** 后续路径查找和目录枚举 MUST 不再返回该文件

#### Scenario: 目录非法删除被拒绝
- **WHEN** 调用方对目录、非空目录、不存在路径或只读后端路径执行删除
- **THEN** BigOS MUST 分别返回确定性错误
- **AND** MUST NOT 修改无关目录项或文件数据

### Requirement: 最小目录枚举可观察运行时目录项

BigOS SHALL 在 `/rw` 可写后端提供最小目录枚举能力，使简单 C 程序和验证路径能够观察文件创建、`mkdir` 与 `unlink` 后的目录项结果。目录枚举 MUST 有界，MUST 至少返回目录项名称和基础类型，MUST 对单次返回字节数、目录项数量和名称长度设置上限，MUST 对非目录、非法 fd、缓冲区不足和用户缓冲非法返回确定性错误。该能力 MUST NOT 承诺完整 POSIX `DIR*`、`struct dirent`、offset cookie、排序、`.`/`..`、跨调用稳定快照或完整目录遍历语义。

#### Scenario: 枚举看到创建的文件和目录
- **WHEN** 简单 C 程序在 `/rw` 目录下创建文件和子目录后执行最小目录枚举
- **THEN** 枚举结果 MUST 在有界输出中包含对应目录项名称和基础类型
- **AND** 结果 MUST 不要求 POSIX 排序或完整 `struct dirent` 兼容

#### Scenario: unlink 后目录枚举不再显示目录项
- **WHEN** 一个常规文件目录项被成功 `unlink`
- **THEN** 后续对其父目录的最小枚举 MUST 不再返回该名称
- **AND** 若该文件仍有打开 fd，fd 引用语义 MUST 按删除已打开文件要求保持有效

#### Scenario: 目录枚举失败确定性返回
- **WHEN** 调用方对非目录 fd、非法 fd、过小输出缓冲或非法用户缓冲执行目录枚举
- **THEN** BigOS MUST 返回确定性错误
- **AND** MUST NOT 修改目录状态或破坏调用方 fd table

### Requirement: fsync 和缓存淘汰语义可观察

BigOS SHALL 保证运行时可写文件的成功写入经 page/buffer cache 立即对后续读可见，并在 `fsync`、显式同步或缓存淘汰写回时写入 RAM-backed 块设备。`fsync` 成功后，即使对应缓存块被淘汰，后续重新读取也 MUST 返回已同步内容；块 IO 失败 MUST 保留脏数据并返回确定性错误。

#### Scenario: fsync 后淘汰再读一致
- **WHEN** 调用方向 `/rw` 文件写入内容、调用 `fsync` 成功、触发或模拟缓存淘汰后重新读取
- **THEN** 读回内容 MUST 与已同步内容一致

#### Scenario: fsync 失败保留可解释状态
- **WHEN** `fsync` 因 RAM-backed 块设备或缓存写回失败而失败
- **THEN** BigOS MUST 返回确定性错误
- **AND** MUST NOT 静默丢弃仍需写回的脏数据

### Requirement: 可写后端支持受限常规文件 rename

BigOS SHALL 在 RAM-backed `/rw` 可写运行时文件系统内支持受限常规文件 `rename`。该操作 MUST 只在同一可写后端内移动或改名常规文件目录项，MUST 经权限、路径长度、父目录存在性、对象类型、容量和目标状态检查后再提交目录项更新，MUST 保持运行期一致性，且 MUST NOT 引入硬链接、符号链接、跨挂载 rename、完整目录 rename、完整 POSIX atomic replacement、journaling 或跨重启持久化承诺。

#### Scenario: 常规文件重命名成功

- **WHEN** 调用方在 `/rw` 内对一个存在的常规文件执行 rename，且源父目录和目标父目录可写、目标名称不存在、容量充足
- **THEN** BigOS MUST 让目标路径在同一运行期内可查找并指向原文件内容
- **AND** 源路径 MUST 不再可查找

#### Scenario: 同一父目录同一名称返回 no-op

- **WHEN** 调用方在 `/rw` 内执行 rename，且源路径和目标路径解析为同一父目录下的同一目录项名称
- **THEN** BigOS MUST 返回成功
- **AND** MUST NOT 修改目录项、inode 元数据、文件数据、fd 引用或 open file offset

#### Scenario: 目标已存在时保守失败

- **WHEN** 调用方把 `/rw` 常规文件 rename 到一个已存在目标路径，且源目标不是同一父目录同一名称
- **THEN** BigOS MUST 返回确定性 `-EEXIST` 或等价目标已存在错误
- **AND** MUST NOT 移除源目录项，MUST NOT 修改目标对象

#### Scenario: 不支持对象和跨后端被拒绝

- **WHEN** 调用方尝试 rename 目录、只读 exFAT 路径、跨挂载路径、缺失源路径、缺失目标父目录或不支持对象类型
- **THEN** BigOS MUST 返回确定性错误
- **AND** MUST NOT 修改 `/rw`、只读 boot assets、exFAT 发现状态或无关目录项

#### Scenario: 失败不发布半成品目录项

- **WHEN** rename 因权限、容量、IO、非法路径或内部状态检查失败
- **THEN** BigOS MUST 保持源路径、目标路径、inode 元数据和文件数据处于失败前的可解释状态
- **AND** MUST NOT panic，内核态 fault 诊断路径除外

### Requirement: rename 后 open file 引用保持稳定

BigOS SHALL 在 rename 改变目录项名称时保持已打开文件对象的引用生命周期稳定。已打开 fd MUST 继续引用原 inode/data blocks；rename 成功后新的路径查找 MUST 看到目标名称，源名称 MUST 不再可见；仍有 open fd 时不得提前释放对应文件对象、inode 或数据块。

#### Scenario: rename 已打开文件后 fd 仍有效

- **WHEN** 进程打开 `/rw` 常规文件后对其路径执行 rename
- **THEN** 已打开 fd MUST 在关闭前继续按打开权限访问同一文件内容
- **AND** 新目标路径的后续 open/read MUST 观察同一运行期文件内容

#### Scenario: rename 不改变 dup 后 fd 共享关系

- **WHEN** 进程 dup 一个文件 fd 后对该文件路径执行 rename
- **THEN** dup 后 fd MUST 继续共享同一 open file object 和 offset
- **AND** rename MUST NOT 破坏 fd 引用计数或进程退出/reap 时的关闭规则

### Requirement: 可写后端权限与提交前检查稳定

BigOS SHALL enforce RAM-backed `/rw` owner/mode, directory writability, object type, capacity, and backend mutability checks before committing file data, inode metadata, directory entries, cache dirty state, or open file offsets. Failed checks MUST return deterministic negative errno values and MUST leave existing runtime filesystem state explainable from the caller's perspective. Permission failures MUST use the existing minimal owner/mode access checks and stable `-EACCES` behavior; this change MUST NOT define complete POSIX directory execute/search bit traversal semantics. This requirement MUST NOT introduce ACLs, extended attributes, full POSIX permission databases, persistent writable storage, or journaling.

#### Scenario: 权限拒绝不修改状态

- **WHEN** 用户进程对 `/rw` 文件或目录执行 create、open-for-write、truncate、write、mkdir、unlink、rename 或 fsync，但 owner/mode 或目录权限检查拒绝该操作
- **THEN** BigOS MUST 返回稳定的 `-EACCES`
- **AND** MUST NOT 修改文件内容、目录项、inode metadata、fd offset、dirty block 状态或目录枚举可见结果
- **AND** MUST NOT require complete POSIX directory execute/search bit semantics as part of this change

#### Scenario: 容量耗尽不发布半成品对象

- **WHEN** `/rw` 操作需要分配 inode、目录项、数据块、缓存块或内核对象但容量耗尽
- **THEN** BigOS MUST 返回确定性容量或内存错误
- **AND** MUST NOT 发布半成品文件、目录、rename 目标、truncate 结果或部分 metadata 更新

### Requirement: 可写后端运行期一致性跨操作可观察

BigOS SHALL make successful `/rw` runtime filesystem changes visible to subsequent operations in the same boot session. Successful create, write, truncate, fsync, mkdir, unlink, and restricted regular-file rename operations MUST produce consistent results across read, open, stat/fstat, minimal directory enumeration, dup/fork/exec-inherited fd references, and independent path reopen. The guarantee MUST remain limited to the current runtime session and MUST NOT imply cross-reboot persistence.

#### Scenario: 成功写入和截断反映到 read 与 stat

- **WHEN** 用户进程在 `/rw` 中创建文件、写入内容、截断或追加，并随后通过同一 fd、dup 后 fd、继承 fd 或重新打开路径读取和查询 metadata
- **THEN** BigOS MUST expose the successful runtime file contents and bounded metadata consistently
- **AND** independent opens MUST retain independent offsets while dup-related fds share the same open file offset

#### Scenario: 目录变更反映到路径查找和枚举

- **WHEN** 用户进程在 `/rw` 中成功执行 mkdir、unlink 或 restricted regular-file rename
- **THEN** later path lookup, open, metadata query, and minimal directory enumeration MUST observe the resulting directory state
- **AND** removed or renamed source paths MUST no longer be found unless still represented by an existing open fd reference

#### Scenario: rename 目标已存在保守失败

- **WHEN** 用户进程在 `/rw` 中执行 restricted regular-file rename，且目标路径已存在并且不是同一父目录同一名称 no-op
- **THEN** BigOS MUST return `-EEXIST`
- **AND** MUST NOT replace the target object, remove the source object, alter open file references, or claim POSIX atomic replacement semantics

#### Scenario: 运行期一致性不承诺重启后保留

- **WHEN** documentation, validation, user tools, or specs describe `/rw` file contents after successful writes or syncs
- **THEN** they MUST describe the guarantee as current-runtime consistency over the RAM-backed writable backend
- **AND** MUST NOT claim persistence after reboot or imply modification of the existing exFAT boot image layout

### Requirement: 只读与可写后端差异保持隔离

BigOS SHALL preserve strict isolation between read-only exFAT boot assets and the RAM-backed `/rw` writable backend. Writable operations MUST be accepted only when the resolved target belongs to the supported writable backend and all bounded checks pass. Read-only metadata queries and reads MUST NOT require or create writable state.

#### Scenario: exFAT 查询和读取不产生可写副作用

- **WHEN** 用户进程读取或查询只读 exFAT 路径的 metadata
- **THEN** BigOS MUST return the bounded read or metadata result without allocating writable backend objects, dirtying cache blocks for exFAT state, or modifying boot assets

#### Scenario: `/rw` 失败不影响 exFAT

- **WHEN** `/rw` operation fails due to permission, capacity, path, object type, user buffer, or IO error
- **THEN** BigOS MUST preserve read-only exFAT mount state and future exFAT path reads
- **AND** MUST NOT use exFAT state as rollback storage or persistence backing for `/rw`
