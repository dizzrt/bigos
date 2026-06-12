## ADDED Requirements

### Requirement: 运行时文件系统行为可被简单 C 程序依赖

BigOS SHALL 将 RAM-backed 可写后端定义为有界运行时文件系统能力，使简单静态 C 程序能够在 `/rw` 范围内可靠使用文件创建、打开、读取、写入、定位、同步、目录创建、最小目录枚举和删除。该能力 MUST 保持运行期一致性，MUST 对路径长度、文件大小、目录项、inode、数据块和打开文件引用设置有界限制，MUST NOT 承诺跨重启持久化、磁盘分区承载、journaling、rename、硬/软链接、ACL/xattr、完整目录遍历、完整 POSIX `readdir/getdents` 兼容或 broad file-backed `mmap`。

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
