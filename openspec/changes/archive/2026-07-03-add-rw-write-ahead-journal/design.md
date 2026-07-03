## Context

当前 `/rw` 由 `bigfs` 提供，默认 RAM-backed，启用 persistent 配置时可挂载独立 persistent test disk。现有 persistent 语义是 clean-sync：`fsync`、显式 sync 或 cache eviction 成功后，数据和元数据可在 clean reboot 后读回，但规格多处明确不承诺 journaling、power-loss safety 或 mount-time crash recovery。

代码层面已经有两个可复用基础：一是 `bigfs` 内部的 bounded metadata commit plan，它把 inode/data bitmap/directory/data blocks 收集后通过 `bcache::sync_block()` 按顺序写回；二是 page/buffer cache 已能按 `(device, block_no)` 执行 selected flush、device-scoped flush 和 dirty failure retention。M15.1 在这些基础上增加 journal-first 提交路径，而不是新增 syscall、后台 writeback 线程或完整恢复器。

## Goals / Non-Goals

**Goals:**

- 在 persistent `/rw` volume 内定义固定有界 journal 区域、日志 header/record/commit marker 和 sequence 语义。
- 让会修改 persistent filesystem state 的路径先写 journal，再写 home-location data/metadata，并在成功后清理或推进 journal tail/head 状态。
- 覆盖 metadata 和 data ordering：目录项、inode、bitmap/free-space、file size、block mapping、truncate/release 与受限 rename 都必须有可重放的 before/after 或 after-image 日志信息。
- 保持 RAM-backed `/rw` 的运行期语义不变；journaled write path 只在 persistent backend 上定义持久化提交语义。
- 保持所有 journal/cache I/O 在可阻塞进程上下文内执行，失败时返回确定性错误且保留 dirty/pending 状态。
- 提供默认关闭验证，证明 journal-first 顺序、失败不误报 durable success、现有只读 boot asset 隔离和未实现 recovery 的边界。

**Non-Goals:**

- 不实现 M15.2 的 mount-time replay/discard，不声称非干净关机后可自动恢复。
- 不引入新的用户态 ABI、syscall、`fdatasync`、mount 工具或完整 POSIX storage management。
- 不支持动态 journal resize、多后端共享 journal、跨 mount transaction、background checkpoint thread 或 async user-visible I/O。
- 不改变 UEFI/Legacy boot 地址、IDT/syscall vector、页表布局、exFAT boot asset 布局或默认启动策略。

## Decisions

1. Journal 区域固定保留在 bigfs persistent volume 内，并提升格式版本；M15.1 固定保留 32 blocks 作为 journal 区域。
   - Rationale: 当前 bigfs volume 为 256 blocks，32-block journal 约占 12.5%，可给最大单事务保留约 24 个 payload blocks，并留下 descriptor、commit marker、checkpoint/clear 与保守余量。相比动态比例或运行时 resize，固定 32 blocks 更接近教学/小型内核常用的 bounded log 策略，也更容易验证最大事务和现有 smoke 数据集。
   - Alternatives considered: 使用外部 journal device 会过早引入 mount/device 管理；动态从 data region 分配 journal 会让 free-space 与恢复设计相互递归。

2. 使用 after-image block journal，而不是逻辑操作 journal。
   - Rationale: 当前 bigfs mutation 已围绕 block cache 和 inode/direct block 表达，after-image record 可以直接覆盖 data block、inode block、bitmap block 和 directory block，后续 M15.2 replay 更简单。
   - Alternatives considered: 记录逻辑操作如 `create/unlink/rename/truncate` 更紧凑，但会把 path lookup、权限和 allocator 决策带入 recovery；before-image rollback journal 更容易回滚，但不适合后续 replay 到新状态。

3. 一个 transaction 覆盖一次高层 filesystem mutation 或一次 `fsync` 需要提交的 dirty set。
   - Rationale: 与现有 `MetadataCommitPlan` 的有界数组一致，能限制 record 数量、栈/静态内存、cache pin 数和错误路径。
   - Alternatives considered: 把多个 syscall 合并成批量 transaction 可减少写放大，但需要 transaction lifetime 管理、open file 协调和更复杂的 failure 状态。

4. 提交顺序为 journal descriptor/data records -> journal commit marker -> home-location selected blocks -> journal checkpoint/clear marker。
   - Rationale: write-ahead 的核心是 home block durable 之前 journal 已 durable；commit marker 区分完整 transaction 与可丢弃 partial journal。
   - Alternatives considered: 只 flush metadata plan 不写 journal 仍是 clean-sync；先写 home 再写 journal 无法支撑 crash recovery。

5. Data ordering 采用 bounded ordered-data journaling：文件内容 block 必须在会发布其 inode size/block mapping 的 metadata home update 前进入 journal 或 durable data path。
   - Rationale: M15.1 目标包含 metadata 与 data ordering；直接记录受影响 data block 的 after-image 可避免 inode 指向未初始化或旧内容。
   - Alternatives considered: 只 journaling metadata 更省空间，但不能满足 roadmap 对数据顺序的要求；完整 data journaling 全模式切换超出阶段范围。

6. Cache 增加明确的 ordered flush helper 或等价能力。
   - Rationale: `sync_block()` 已可按 block 写回，但 journal commit 需要把一组 journal blocks 与 home blocks 按阶段失败传播，并阻止 eviction 绕过 ordering。
   - Alternatives considered: 在 bigfs 内重复扫描 cache 会破坏 cache ownership；全局 `sync_all()` 会扩大 durable scope，和现有 device-scoped 纪律冲突。

7. M15.1 mount 行为只识别 journal-capable format，不执行 replay。
   - Rationale: 这是 M15.2 的职责。M15.1 在 mount-time 检测到 committed but not checkpointed journal 时，必须拒绝把该 persistent volume 发布为 writable `/rw`；普通启动可按既有策略 fallback 到 RAM-backed `/rw`，但必须记录明确诊断。这样既保留启动可用性，又不把未恢复的 persistent state 误称为干净状态。
   - Alternatives considered: 立即实现 replay 会扩大任务边界；忽略 journal 状态直接挂载会损害一致性纪律。

## Risks / Trade-offs

- Journal 区域占用原 data region，降低小型 persistent volume 可用容量 -> 通过固定容量、格式版本提升、mkfs 初始化和验证中的 capacity case 显式覆盖。
- After-image journaling 写放大明显 -> 本阶段优先选择可验证性和后续 replay 简单性，容量上限保持有界。
- Dirty eviction 可能在 transaction 顺序外写 home block -> cache 需要识别 journal-protected home block 或由 bigfs 在 transaction 期间 pin/mark pending，eviction 不得 unordered publish。
- Partial implementation 容易被误解为 crash recovery 已完成 -> specs、backend name/diagnostics、validation 和 docs 明确 M15.1 不执行 replay/discard。
- 格式版本提升会使旧 persistent image 不兼容 -> mount 必须 deterministic reject 或要求 explicit format，不能自动迁移或 silent reinterpret。

## Migration Plan

1. 提升 bigfs persistent format version，定义固定 32-block journal range、superblock 字段和 mkfs 初始化逻辑；旧版本 persistent image mount 时确定性拒绝。
2. 在 bigfs 内新增 journal transaction builder，将现有 metadata commit plan 改为生成 journal records + ordered home commit plan。
3. 扩展 bcache selected ordered flush 能力，并补齐 dirty/pending failure semantics。
4. 将 create、write growth、truncate、unlink、rmdir、rename、utimens、format 和 fsync 路径逐步接入 journal-first commit。
5. 增加默认关闭 validation，覆盖正常 journal commit、容量耗尽、I/O failure retention、旧格式拒绝和未实现 recovery 的 mount 边界。
6. 回滚策略：如果 journal-capable persistent mount 或 format 失败，保持 RAM-backed fallback 或 persistent `/rw` unpublished 的现有策略；不得改动只读 exFAT boot asset。

## Open Questions

- 无。
