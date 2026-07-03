## Why

M15.1 需要把现有 persistent `/rw` 从 clean-sync 边界推进到具备 write-ahead journaling 的写入路径，使文件数据、目录项、inode 与 free-space 元数据更新在提交前先有可解释的有界日志记录。这样可以为 M15.2 的 mount-time replay/discard 恢复路径建立磁盘格式与提交顺序基础，同时保持当前阶段的范围可验证。

## What Changes

- 为 persistent `/rw` 引入有界 write-ahead journal 区域、日志记录格式、事务边界和提交标记。
- 将 `/rw` 的 create、write growth、truncate、unlink、rmdir、restricted regular-file rename、metadata update 和 `fsync`/sync 路径接入 journal-first 提交流程。
- 规定数据块、inode、目录块、bitmap/free-space 元数据与 superblock/sequence 元数据之间的写入顺序：journal record 必须先于对应 home-location 更新 durable。
- 扩展 page/buffer cache 的 selected-block/ordered-commit 能力，使 journal block 与 home metadata block 能按明确计划写回，并在失败时保留 dirty 或 pending 状态。
- 增加默认关闭的 journaling validation，覆盖成功提交、提交中写回失败、容量耗尽、只读 boot asset 隔离和“不会声称恢复已完成”的边界。
- 非目标：不实现 mount-time journal replay/discard、crash injection 后自动恢复、完整 POSIX `fsync`/`fdatasync` 语义、ordered/writeback/data journaling 全模式切换、动态 journal resize、跨 mount/backend journal、broad storage management 或新的用户态系统调用。

## Capabilities

### New Capabilities

- `rw-write-ahead-journal`: 定义 persistent `/rw` 的有界 write-ahead journal 区域、日志记录、事务状态、提交顺序、失败语义和验证边界。

### Modified Capabilities

- `persistent-writable-filesystem`: 将 clean-sync `/rw` 的持久提交路径扩展为 journal-first，但仍不声称 M15.2 的 mount-time crash recovery。
- `page-buffer-cache`: 为 journal 与 home metadata/data 提交提供 selected dirty block ordering、失败传播和 dirty/pending 保留要求。
- `runtime-filesystem-maturity`: 更新文件系统能力边界描述，区分 RAM-backed runtime、persistent clean-sync、journaled write path 与未来 recovery 的责任。

## Impact

- 受影响子系统：`kernel/core/fs/bigfs.cc`、`kernel/core/fs/bcache.cc`、VFS `fsync`/sync 调用链、persistent writable smoke/validation、相关 public/internal FS headers 与文档/spec。
- 架构与启动假设：继续以 x86_64 UEFI 默认路径和 Legacy BIOS 兼容验证路径为运行目标，不改变 kernel higher-half 地址、IDT/syscall vector、页表布局、exFAT boot asset 发现方式或默认只读 boot image。
- 磁盘布局假设：persistent `/rw` 使用现有独立 persistent test disk/backing device；journal 区域属于 bigfs persistent volume 格式的一部分，普通 boot asset/exFAT 镜像不被修改。
- 工具链与验证假设：继续使用 xmake、x86_64-elf-gcc、QEMU/Bochs 与默认关闭 smoke；依赖不可用时记录 skipped/blocked，不声称 runtime-passed。
