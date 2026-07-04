## Why

M15.1 已经为持久化 `/rw` 建立了 journal-first 写入路径，但非干净关机后仍只能拒绝持久化可写挂载并回退到 RAM-backed `/rw`。M15.2 需要补上挂载时恢复路径，使 journal-capable bigfs 能在可判定的日志状态下恢复到一致文件系统，而不是把崩溃后一律视为不可发布。

## What Changes

- 为持久化 `/rw` 增加挂载时 journal recovery 流程，覆盖 committed-but-not-checkpointed 事务的 replay，以及未提交或损坏事务的保守 discard/reject 策略。
- 将 M15.1 的“检测到脏日志时拒绝可写挂载”边界升级为 M15.2 的恢复前置检查、事务校验、home-location 回放、checkpoint/clear 和发布决策。
- 增加 recovery 诊断与默认关闭验证，能区分 clean mount、successful replay、safe discard、unsupported/corrupt journal reject、RAM-backed fallback。
- 保持当前 x86_64 UEFI 默认路径、Legacy 交叉验证路径、BootInfo v2、页表布局、syscall ABI、只读启动资产与 bigfs journal 区大小不变。
- 非目标：不实现多事务并发、不实现跨卷恢复、不实现在线 fsck、不改变现有 `/rw` 挂载点数量，也不扩大为完整 POSIX fsync/power-loss 证明。

## Capabilities

### New Capabilities
- `journal-mount-recovery`: 定义 journal-capable bigfs 在挂载时识别、校验、replay、discard 或拒绝 journal 状态，并在恢复后发布一致的持久化 `/rw`。

### Modified Capabilities
- `rw-write-ahead-journal`: 将 M15.1 中“不声明 mount-time recovery”的要求更新为：journal 写入格式与顺序必须为 M15.2 recovery 提供足够的事务边界、校验和 checkpoint 状态。

## Impact

- 影响子系统：`kernel/core/fs` 中 bigfs 持久化 `/rw` 挂载、journal 元数据解析、事务记录校验、block cache/flush 排序、fallback 诊断与默认关闭 smoke。
- 影响测试与工具：需要新增或扩展持久化 bigfs 镜像构造、unclean journal 状态注入、recovery 后文件/目录/元数据可见性验证，以及损坏日志拒绝挂载验证。
- 架构与环境假设：继续限定 x86_64、C++17/C17 freestanding、xmake、x86_64-elf-gcc、QEMU 默认 UEFI 路径；Bochs/Legacy 仅作为显式兼容验证路径。
- 磁盘布局假设：bigfs v3 journal-capable 格式继续使用固定 32 blocks journal 区；本变更不扩大 journal 区、不迁移旧格式、不修改 exFAT boot asset。
