## Context

BigOS 当前已经具备内核块设备读写、设备框架发布块设备、page/buffer cache、persistent `/rw` clean-sync 和有界文件系统能力。现有路径适合当前同步 ATA PIO 后端，但 cache 与文件系统仍直接面对块设备执行入口，缺少统一的请求对象、排队点、完成状态和诊断边界。

本设计在现有 x86_64 Legacy BIOS/MBR/exFAT/bigfs 默认路径上引入块 I/O 请求层。该层位于 page/buffer cache 与设备框架发布的 `BlockDevice` 接口之间：上层提交读写请求，请求层进行有界校验、排队、同步执行和状态传播，底层仍调用现有块设备读写实现。该变更不改变 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局或用户态 ABI。

## Goals / Non-Goals

**Goals:**
- 提供 freestanding-safe 的块 I/O 请求描述与有界队列。
- 支持当前同步读写提交，返回确定性完成状态。
- 让 page/buffer cache 的读装入、写回、设备范围同步和 dirty victim 淘汰通过请求层提交块 I/O。
- 保留现有块设备读写校验、设备框架发布契约、persistent `/rw` clean-sync 语义和默认关闭 smoke 验证方式。
- 为未来 async I/O 保留请求状态和完成边界，但当前实现仍同步完成。

**Non-Goals:**
- 不实现 async I/O、DMA、多队列、elevator 调度、I/O 合并、超时线程或后台 flush 线程。
- 不新增 virtio、AHCI/SATA、NVMe、USB storage 或第二块设备后端。
- 不引入 SMP I/O 并发、跨 CPU 队列、IPI 通知或新的 locking 模型。
- 不暴露用户可见设备节点、syscall ABI、分区管理或完整 POSIX 块设备模型。
- 不改变 UEFI spike 的 runtime parity 状态。

## Decisions

1. 请求层采用有界静态队列，而不是动态无界分配。
   - 原因：内核仍处于 freestanding、单核、同步为主的阶段，固定容量更容易验证失败路径，并避免在存储错误路径中引入不可控内存压力。
   - 备选：每次提交动态分配请求对象。该方案实现简单但失败点分散，且在 cache 淘汰/写回错误路径中更难保持 deterministic 行为。

2. 当前提交 API 同步等待完成，但保留请求状态字段。
   - 原因：路线目标需要“为未来 async I/O 预留空间”，但当前内核没有后台 worker、SMP 调度或 async completion 基础。同步 API 可以先统一 cache 与块设备的边界。
   - 备选：直接实现 async callback。该方案会提前扩大调度、生命周期、锁与不可阻塞上下文风险，不适合当前阶段。

3. 请求层不替代设备框架，设备解析仍通过已发布的块接口完成。
   - 原因：设备框架负责注册、probe、发布和稳定角色；请求层只负责任务提交、队列和完成状态，两个边界应保持单一职责。
   - 备选：请求层内部按角色查找设备。该方案会把设备生命周期和 I/O 调度耦合，增加初始化顺序风险。

4. page/buffer cache 迁移为请求层主要消费者。
   - 原因：cache 是现有块 I/O 的核心上层调用方，覆盖装入、写回、sync 和淘汰路径。让 cache 通过请求层可以验证队列容量、错误传播和 dirty 状态保留。
   - 备选：先只给新代码使用请求层，保留 cache 直连块设备。该方案无法证明“缓存对接”目标，也会形成两个并行 I/O 契约。

5. 不改变底层 `BlockDevice` 扇区大小和整扇区读写契约。
   - 原因：现有 ATA PIO、MBR/exFAT 发现和 persistent `/rw` 都建立在明确扇区语义上。请求层应复用该稳定契约，而不是重新定义磁盘格式或块大小。
   - 备选：在请求层引入可变大小 bio/segment。该方案更接近通用 OS，但会扩大范围到 scatter/gather 和跨页生命周期。

6. 请求队列容量采用按块设备固定值，而不是单一全局队列。
   - 原因：设备框架已经按稳定角色发布块设备，per-device 固定队列能让 boot disk、persistent disk 或后续验证后端的请求压力彼此隔离，同时仍保持每个队列的内存占用有界。当前同步提交路径不会引入复杂并发，但这个边界更贴近后续第二块设备后端和 async I/O 演进。
   - 备选：使用单一全局固定队列。该方案更简单，但一个设备的 cache 写回或验证请求可能耗尽全局槽位，导致另一个设备出现不必要的 backpressure，诊断也较难区分。

7. 请求层增加少量专用状态，并规范化或透传底层设备状态。
   - 原因：请求层需要表达 `InvalidRequest`、`QueueFull`、`NotReady` 等“请求尚未或无法发给设备”的错误；底层 `Timeout`、`DeviceError`、`Unsupported`、`ReadOnly` 等状态则应继续保留设备语义。这样 cache 可以区分队列/提交失败与真实设备写失败，dirty/pending 状态也更容易解释。
   - 备选：完全复用块设备状态。该方案减少枚举数量，但会把队列满、请求无效、设备未发布和硬件错误混在一起，增加上层错误映射和验证诊断的不确定性。

## Risks / Trade-offs

- [Risk] 请求层增加一次间接调用和队列管理，可能让当前同步路径更复杂。→ Mitigation：保持请求对象字段最小化，并让同步提交路径直接执行单个请求，队列仅承担有界顺序与状态记录。
- [Risk] cache 迁移时错误传播不完整会错误清除 dirty 状态。→ Mitigation：规范要求请求失败时 cache 保留 dirty 或 pending 状态，并在任务中单独覆盖 writeback/eviction 失败路径。
- [Risk] 队列容量耗尽可能在已有同步路径中引入新的失败码。→ Mitigation：容量耗尽必须确定性返回错误，调用方不得 panic 或伪造成功；验证覆盖队列满场景。
- [Risk] 未来 async 预留字段可能被误认为已支持异步完成。→ Mitigation：文档、spec 和任务明确当前只支持同步完成，不暴露 async API 承诺。
- [Risk] 设备框架、请求层、cache 的初始化顺序不清晰可能导致早期挂载失败。→ Mitigation：实现顺序要求先发布块设备，再初始化请求层消费者；失败以确定性初始化错误上报。

## Migration Plan

1. 新增块 I/O 请求层的数据结构、状态码、队列初始化和同步提交入口。
2. 将请求执行端接到设备框架发布的 `BlockDevice` 读写入口，保持原有读写校验和错误码映射。
3. 迁移 page/buffer cache 的读装入、写回、设备范围同步和 dirty victim 淘汰路径，使其通过请求层提交 I/O。
4. 保留底层块设备 API 作为请求层执行接口，逐步清理普通上层消费者的直接调用。
5. 增加默认关闭验证，覆盖同步读写、队列满、cache 写回失败保留 dirty、persistent `/rw` clean-sync 不回退为伪成功。
6. 如果验证失败，回滚策略是保留底层块设备 API 与设备框架路径，撤回 cache 对请求层的调用点，不改变磁盘格式或用户态 ABI。

## Open Questions

- 暂无。请求队列采用按块设备固定容量；请求层采用少量专用状态并规范化或透传底层设备状态。
