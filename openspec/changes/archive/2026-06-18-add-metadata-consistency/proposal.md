## Why

当前 persistent clean-sync `/rw` 已能跨 clean reboot 保存同步后的文件和目录状态，但目录项、inode、free-space metadata、文件 size 与块映射之间还缺少明确的持久提交顺序和失败语义。下一步需要把元数据持久化从“同步后可见”推进到“有序、可解释、可验证”，为可靠多文件持久写入打基础。

本变更为持久 `/rw` 定义最小一致性策略，优先采用 bounded ordered writes；它约束成功同步、失败回滚和重新挂载后的可观察状态，但不声明完整 journal、crash recovery 或 power-loss recovery。

## What Changes

- 为 persistent `/rw` 定义元数据有序提交能力：目录项、inode、文件大小、块映射、free-space metadata 和 superblock/format metadata 的更新必须按可解释顺序同步。
- 规定 create、unlink、rmdir、rename、file growth、truncate 和 metadata update 的 durable commit 边界，成功 `fsync`、显式 sync 或受控同步完成后才能声明跨 clean reboot 可见。
- 规定失败语义：容量耗尽、cache block 耗尽、内核分配失败、块 IO 失败或同步失败不得发布半成品目录项、孤儿 inode、重复块所有权、丢失的 free-space metadata 或 dirty-cache success。
- 增加 remount-time metadata 校验与确定性拒绝路径，使不兼容或内部不一致的持久卷不会被误挂载为可写 `/rw`。
- 扩展 default-off 验证，覆盖元数据提交顺序、失败注入或可模拟失败路径、双阶段 clean reboot 读回和不扩大 crash recovery 承诺的验证记录。
- 不引入完整 POSIX filesystem、完整 journal replay、fsck、mount namespace、async I/O、新块层框架、广泛存储设备支持、SMP 或 UEFI runtime parity。

## Capabilities

### New Capabilities

- `metadata-consistency`: 覆盖 persistent `/rw` 元数据持久化、有序提交、同步成功边界、同步失败语义、重新挂载校验和验证边界。

### Modified Capabilities

- `persistent-writable-filesystem`: 将目录项、inode、free-space metadata、文件大小与块映射的 ordered metadata commit 纳入 persistent clean-sync `/rw` 跨 clean reboot 可见性要求。
- `writable-directory-tree`: 要求 persistent 后端目录树 mutation 的元数据提交保持有序，失败不发布半成品目录项或泄漏 inode/block 所有权。
- `stable-file-growth`: 要求 persistent 后端文件增长、截断和块复用的元数据提交顺序保持可解释，失败不发布 durable size 或块映射。
- `page-buffer-cache`: 要求 metadata dirty blocks 的同步、淘汰写回和失败保留 pending state 能支撑 ordered metadata commit。

## Impact

- 受影响子系统：`kernel/core/fs` 中的 persistent `/rw` 后端、`bigfs` 元数据路径、VFS 同步路径、目录树 mutation、文件增长/截断路径、free-space metadata 和 page/buffer cache。
- 受影响用户态：最小 shell 或小型验证程序可扩展用于触发 create/unlink/rename/truncate/fsync 序列；不要求完整 POSIX 工具链。
- 验证影响：新增或扩展 default-off filesystem/persistent smoke，优先覆盖当前 x86_64 Legacy BIOS 路径；若 xmake、x86_64-elf toolchain、QEMU/Bochs、ROM/display 或持久测试盘不可用，必须记录 blocker、跳过原因和残余风险。
- 架构和布局假设：继续以单核 x86_64 Legacy BIOS/MBR/exFAT 默认运行路径为目标，复用现有同步块 IO 与 page/buffer cache；不修改 boot handoff ABI、启动磁盘资产布局、页表布局、IDT/syscall vector、syscall number 排列或 UEFI backend parity。
