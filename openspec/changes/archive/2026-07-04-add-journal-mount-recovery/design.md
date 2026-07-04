## Context

M15.1 已经把 persistent `/rw` 的 bigfs 升级为 journal-capable v3，并固定保留 32 blocks journal 区。当前写入路径能按 journal-first 顺序提交单事务，但 mount-time 行为仍是保守保护：如果发现 committed but not checkpointed journal，不能发布 persistent writable `/rw`，只能拒绝或回退到 RAM-backed `/rw` 并记录诊断。

M15.2 的任务是在不改变磁盘布局、boot ABI、syscall ABI、页表布局和只读启动资产的前提下，实现挂载时恢复。恢复路径需要在挂载发布前读取 journal metadata，判定事务状态，校验 record/commit/checkpoint 信息，并对可恢复事务执行 replay 或对未提交事务执行安全 discard；不可理解或损坏状态仍必须拒绝持久化可写挂载。

## Goals / Non-Goals

**Goals:**

- 在 persistent `/rw` mount validation 中加入 journal recovery 阶段，覆盖 clean、uncommitted、committed-uncheckpointed、checkpointed/clear、corrupt/unsupported 状态。
- 对 committed-but-not-checkpointed transaction 执行 bounded replay，将 journal after-image records 写回 home-location blocks，并在成功后 checkpoint/clear journal。
- 对没有 durable commit marker 的 partial transaction 执行安全 discard/clear，前提是校验能证明 home-location blocks 未依赖该未提交事务。
- 对 checksum、sequence、bounds、record count、block target、commit marker 或 checkpoint 状态异常的 journal 执行 deterministic reject，不发布 persistent writable `/rw`。
- 保持 recovery 在可阻塞 mount-time 上下文运行，失败时保留足够诊断，普通启动可以按既有策略回退 RAM-backed `/rw`。
- 增加默认关闭验证，构造 unclean journal 状态并证明 replay、discard、reject 和 fallback 诊断可观察。

**Non-Goals:**

- 不扩大 32-block journal 区，不引入动态 journal resize、外部 journal device 或多事务并发恢复。
- 不实现在线 fsck、跨卷恢复、后台 checkpoint thread、用户态 mount 工具或新的 syscall。
- 不声明完整 POSIX power-loss 证明，不覆盖硬件 write-cache flush 语义以外的真实掉电模型。
- 不改变默认 RAM-backed `/rw` 行为、不改变只读 exFAT boot asset、不改变 M15.3 之前的单可写挂载边界。

## Decisions

1. Recovery 作为 persistent bigfs mount validation 的前置阶段执行。
   - Rationale: `/rw` 发布前仍没有用户态并发访问，replay 可以使用简单的同步 block I/O 和现有 cache ordering，不需要运行期锁层级或后台线程。
   - Alternatives considered: 在第一次 `fsync` 或第一次文件访问时 lazy recovery 会让脏状态暴露给 VFS；启动后用户态工具修复需要先挂载不可信文件系统。

2. 只 replay durable committed transaction，discard 只用于未提交且可验证未发布的 partial journal。
   - Rationale: commit marker 是 M15.1 journal-first 顺序的 durable 边界。没有 commit marker 的日志不能代表成功 mutation；有 commit marker 的日志必须完成 home-location 回放以恢复一致状态。
   - Alternatives considered: 对所有异常状态尝试 best-effort replay 会放大损坏；一律拒绝会无法满足 M15.2 恢复目标。

3. Replay 使用 journal after-image records 覆盖 home-location blocks，并按记录顺序同步。
   - Rationale: M15.1 选择 after-image block journal，就是为了让 recovery 避免重新执行 path lookup、allocator 决策或高层 VFS 操作。record 目标 block 必须在 data region/metadata region 的合法范围内，且不能指向 journal 区自身，除非是明确的 journal control block 更新。
   - Alternatives considered: 逻辑操作 replay 更紧凑，但需要重建运行期 inode/目录语义；before-image rollback 不符合已提交事务应前滚的语义。

4. Journal state machine 显式区分 `clean`、`partial`、`committed`、`checkpointed`、`corrupt`。
   - Rationale: 状态名让 mount 诊断、smoke marker 和错误路径稳定可测，也能避免把损坏日志误归为可丢弃 partial transaction。
   - Alternatives considered: 用布尔 dirty/clean 简化实现会丢失 commit/checkpoint 的关键区别。

5. Recovery I/O 采用 fail-closed 策略。
   - Rationale: 如果 replay 写 home block、sync、checkpoint 或 clear 失败，下一次 mount 仍应看到可恢复或可诊断的 journal；不得把 filesystem 发布为 clean writable state。
   - Alternatives considered: 失败后发布 read-only persistent `/rw` 当前 VFS 尚未定义；本阶段保持拒绝 persistent writable `/rw` 与 RAM fallback 策略更小。

6. 验证使用默认关闭 smoke 和受控镜像状态注入，而不是依赖真实断电。
   - Rationale: 当前项目的 runtime validation 已采用 xmake gated smoke 与串口 marker。M15.2 可以通过构造 journal control blocks/records 模拟 unclean shutdown 的 durable disk 状态，覆盖 determinism。
   - Alternatives considered: 在 emulator 中硬杀进程更接近真实崩溃，但结果受 host buffering 与镜像缓存影响，难以作为第一阶段确定性验证。

## Risks / Trade-offs

- [Risk] partial journal 是否已把 home block 提前写出难以仅靠日志判断 -> Mitigation: M15.1 的 ordered commit 必须禁止 commit marker 前发布 home-location updates；M15.2 discard 仅依赖该已规格化顺序，并保留验证用 failure injection。
- [Risk] replay 中途失败可能留下部分 home blocks 已更新 -> Mitigation: commit marker 保留到 checkpoint/clear 成功；下一次 mount 继续 replay 同一 after-image transaction，要求 replay 幂等。
- [Risk] corrupt journal 被误判为 partial 导致丢失 committed update -> Mitigation: sequence、checksum、record count、bounds 和 commit marker 校验任一异常都进入 reject，而不是 discard。
- [Risk] 32-block journal 容量限制使复杂 mutation 无法恢复 -> Mitigation: M15.2 不改变 M15.1 的单事务上限；超限 mutation 继续 deterministic error，不生成不可恢复事务。
- [Risk] cache 中残留 dirty home blocks 影响 recovery 顺序 -> Mitigation: mount-time recovery 在发布 VFS 前运行，并对目标 device 执行明确的 recovery read/write/sync discipline；失败不发布 persistent writable `/rw`。

## Migration Plan

1. 保持 bigfs v3 和固定 32-block journal layout；在 mount validation 中加入 recovery state scan。
2. 实现 journal record/commit/checkpoint 校验与状态分类，确保旧格式、不支持版本和 bounds 异常仍确定性拒绝。
3. 实现 committed transaction 的幂等 replay 与 checkpoint/clear；实现可验证 partial transaction 的 discard/clear。
4. 将 persistent `/rw` 发布策略接入 recovery 结果：clean/replayed/discarded 才可发布，corrupt/unsupported/I/O failure 必须拒绝并允许 RAM fallback 诊断。
5. 增加默认关闭 validation：构造 clean、partial、committed-uncheckpointed、corrupt journal 镜像，验证 mount 后文件系统可见性和诊断 marker。
6. 回滚策略：如 recovery 实现或验证失败，保留 M15.1 的拒绝 dirty journal 行为，不得把未恢复 persistent volume 发布为 writable `/rw`。

## Open Questions

- 无。
