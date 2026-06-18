## Why

当前持久 `/rw` 已具备有界目录树、文件增长、truncate 和元数据一致性契约，但同步路径仍需要明确收束到 page/buffer cache 的脏块回写能力上。这个 change 目标是让 `fsync`、显式同步和缓存淘汰都能通过统一缓存回写路径把数据与必要元数据可靠写入后端，并在失败时保留可解释的 pending/dirty 状态。

## What Changes

- 为 page/buffer cache 增量定义面向持久 `/rw` 的选择性回写、全量同步、淘汰回写和失败保留语义。
- 为持久 `/rw` 增量定义 `fsync`/显式同步经缓存回写后才报告 clean-sync success 的行为。
- 为元数据一致性增量定义提交计划如何驱动缓存按序写回数据块、inode、目录项、free-space 和卷元数据。
- 增加有界用户态 `sync()` 能力：kernel syscall、libc wrapper 和 shell 内建命令，把当前 writable backend 的 dirty state 同步到后端；该能力不声明完整 POSIX `sync(2)`。
- 新增 `sync_device()` 或等价 device-scoped cache API，保留 `sync_all()` 作为调试/全局内部工具。
- 补充默认关闭验证要求，覆盖同步后 clean reboot 读回、淘汰后重载读回、回写失败不清 dirty、不在不可阻塞上下文发起阻塞 IO。
- 保持当前 x86_64 Legacy BIOS 默认运行目标、单核同步模型、现有磁盘布局和 bounded POSIX-like 文件语义，不引入 journaling、crash recovery、async I/O、SMP 或新的存储设备框架。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `page-buffer-cache`: 增加持久 `/rw` 所需的缓存回写选择、错误传播、淘汰回写和不可阻塞上下文拒绝语义。
- `persistent-writable-filesystem`: 明确持久 `/rw` 的 `fsync`、显式同步和缓存淘汰成功必须经 page/buffer cache 写到后端后才扩大 clean-sync 承诺。
- `metadata-consistency`: 明确 ordered commit 计划如何通过缓存回写路径同步数据与元数据，并在失败时保留 dirty/pending 状态。
- `fd-vfs-shell`: 增加内核/VFS 显式同步入口，把 bounded writable backend 的 dirty state 经 device-scoped cache path 同步。
- `user-libc-min`: 增加 `sync()` libc wrapper 与 errno 翻译。
- `user-shell`: 增加 `sync` shell 内建命令，调用 libc `sync()` 并报告确定性错误。
- `posix-like-process-io-subset`: 将有界显式同步纳入当前 POSIX-like I/O 子集，同时保持非完整 POSIX `sync(2)` 边界。

## Impact

- 影响子系统：`kernel/core/fs` 中的 page/buffer cache、bigfs 持久 `/rw` 后端、VFS `fsync`/同步路径、syscall 分发、用户态 libc/shell、默认关闭文件系统验证路径。
- API/ABI：新增一个有界同步 syscall/libc wrapper/shell builtin；不改变既有 syscall 编号语义、用户态结构体 ABI、启动 ABI、IDT/syscall vector、页表布局、MBR/exFAT boot asset 布局或默认只读 boot asset 发现路径。
- 架构假设：当前交付目标仍为单核 x86_64 Legacy BIOS 路径；UEFI backend spike、SMP 和多架构后端不在本 change 范围。
- 存储假设：复用现有持久测试盘和 bounded persistent `/rw` 介质，不新增 broad storage/device 支持，也不引入新的块层抽象。
- 工具链/验证假设：实现验证以 xmake、`x86_64-elf-*` 交叉工具链、QEMU/Bochs 可用性和默认关闭 smoke 为前提；环境不可用时记录 skipped/blocked 和残余风险，不声称 runtime passed。
