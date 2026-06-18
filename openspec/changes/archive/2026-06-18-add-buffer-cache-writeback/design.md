## Context

BigOS 当前 `/rw` 路径已经具备 RAM-backed 运行期可写后端和 persistent clean-sync 后端，文件数据与元数据均通过 page/buffer cache 访问。现有缓存提供按块获取、标脏、单块同步、全量同步和设备失效接口；bigfs 已有有界 metadata commit plan，用于把 inode、目录项、bitmap、数据块等参与提交的块按计划同步。

本 change 的设计目标不是重写文件系统，而是把“同步可靠落盘”收束成明确的缓存回写路径：所有持久 clean-sync 成功都必须由 page/buffer cache 对相应 dirty blocks 的成功写回支撑；失败路径必须保留 dirty 或 pending 状态，不能把未写成功的状态说成 durable。

约束：
- 当前交付目标仍是单核 x86_64 Legacy BIOS 路径。
- 当前块 IO 是同步阻塞模型，cache load/write-back 只能发生在普通可阻塞进程上下文。
- 不改变启动地址、链接地址、页表布局、IDT/syscall vector、MBR/exFAT boot asset 布局；本 change 会新增一个有界用户态 `sync()` syscall/libc/shell surface。
- 验证依赖 xmake、`x86_64-elf-*` 交叉工具链、QEMU/Bochs 和持久测试盘；不可用时必须记录 skipped/blocked。

## Goals / Non-Goals

**Goals:**
- 让 `fsync`、显式同步和缓存淘汰通过统一 page/buffer cache write-back 路径把 dirty data 和 metadata 写到 backing store。
- 支持按设备、按块集合或按 commit plan 的有界选择性回写，并把首个确定性错误传播给 VFS/bigfs/syscall 调用方。
- 新增 `sync_device()` 或等价 device-scoped cache API，保留 `sync_all()` 作为调试/全局内部工具。
- 新增有界用户态 `sync()` wrapper 和 shell `sync` builtin，用于同步当前 writable backend dirty state。
- 在回写失败时保留 dirty/pending 状态，避免 silent data loss 或虚假的 clean-sync success。
- 保持 metadata ordered commit 的数据先行、发布元数据后行、释放元数据有序化约束。
- 用源码检查和默认关闭 runtime smoke 覆盖 clean reboot 读回、淘汰再读、失败保留和不可阻塞上下文拒绝。

**Non-Goals:**
- 不引入 journaling、journal replay、crash recovery、power-loss recovery 或 unclean shutdown 修复。
- 不新增 async I/O、request queue、device framework、第二块设备后端或 broad storage driver。
- 不实现完整 POSIX `sync(2)`/`fdatasync(2)` 兼容、完整 POSIX 文件系统、硬/软链接或 broad file-backed writable `mmap`。
- 不启用 SMP，不引入跨 CPU cache coherency、IPI 或 TLB shootdown 行为。
- 不改变 UEFI backend spike 的运行时等价状态。

## Decisions

1. 以 page/buffer cache 作为唯一持久 dirty block 写回入口。

   bigfs 的数据块、inode block、目录项 block、bitmap block 和 volume metadata block 都已经通过缓存读写。继续沿用这一边界，可以避免绕过缓存导致的 stale read、dirty bit 丢失或同一块双通道写入。备选方案是在 bigfs 中直接调用底层 block device 写元数据；该方案会破坏缓存视图一致性，且更难在失败时保留 dirty state。

2. `fsync` 和用户态 `sync()` 先完成 pending metadata commit，再执行设备范围 dirty block 同步。

   metadata commit plan 表达有序提交约束，必须在设备 dirty sync 前优先处理。这样可以保证显式同步不会绕过尚未完成的 ordered metadata plan。备选方案是只调用全局 `sync_all()`；该方案无法表达“哪些块属于本次发布语义”和提交顺序，在删除、truncate 和 free-space 更新中风险更高。

3. 淘汰 dirty block 必须复用同一 `write_back` 失败语义。

   缓存容量耗尽时允许选择 unreferenced dirty victim，但复用前必须成功写回。失败时 victim 保持 dirty 且 slot 不复用，调用方得到确定性容量/IO 失败。备选方案是忽略淘汰写回失败继续复用 slot；该方案会丢失唯一 dirty 副本。

4. 新增 `sync_device()` 或等价 device-scoped API，保留 `sync_all()` 作为调试/全局内部工具。

   持久 `/rw` 的 clean-sync 承诺只应该覆盖自身 backing device。新增 device-scoped API 可以让 `bigfs::fsync()`、VFS 显式同步和用户态 `sync()` 只同步当前 writable backend，而不把其他缓存设备的 dirty state 一并纳入成功语义。保留 `sync_all()` 便于调试和后续内部维护，但生产同步路径不依赖它扩大承诺。备选方案是把 `sync_all()` 改成 device-scoped 默认行为；该方案会破坏名称直觉，也可能影响现有调试调用点。

5. 选择性回写接口保持有界、同步和显式。

   对 metadata commit plan 使用固定数组记录 block numbers，并逐块调用缓存选择性同步。后续实现可以把接口整理为 device-scoped batch sync，但不能引入动态分配或异步队列。备选方案是引入通用 request queue；这属于后续块层/设备框架方向，会扩大本 change 范围。

6. 增加 bounded 用户态 `sync()` 能力。

   内核新增显式同步 syscall，libc 暴露 `int sync(void)` wrapper，shell 增加 `sync` builtin。该调用同步当前 writable backend 的 dirty state：RAM-backed backend 只保证当前运行期可解释，persistent backend 在成功后扩大 clean-sync 承诺。该能力不提供完整 POSIX `sync(2)` 的全系统语义，也不保证异步 flush、后台写回或其他挂载命名空间。备选方案是只保留 `fsync(fd)`；但用户明确需要 shell/libc 可触达的显式全后端同步能力，且这能直接验证 cache write-back path。

7. 不可阻塞上下文采用轻量 guard 加调用点约束。

   cache load/write-back 可能分配内核页并执行同步块 IO，不能从 IRQ、scheduler critical section、preemption-disabled 区域调用。实现优先复用现有 scheduler/preemption 状态查询来加轻量运行时 guard；如果某类上下文状态没有稳定查询接口，则先用调用点约束、源码级检查和文档化诊断覆盖，不为本 change 新造通用上下文跟踪机制。

## Risks / Trade-offs

- [Risk] 现有 `sync_all()` 是全局缓存同步，可能写回无关设备 dirty blocks。→ Mitigation: 增加或使用 device-scoped/selective 同步路径，持久 `/rw` 的 `fsync` 只扩大自身 backing device 的 clean-sync 承诺。
- [Risk] metadata commit plan 容量不足会导致本可完成的操作失败。→ Mitigation: 保持固定上限并把不足映射为确定性 `NoSpace`/`IoError`，验证覆盖容量边界；不为本 change 引入动态 plan 分配。
- [Risk] 回写失败后 dirty blocks 长期滞留，后续淘汰和 invalidate 可能反复失败。→ Mitigation: 保留 dirty/pending 状态并向同步调用方持续返回错误，文档和验证不宣称该状态 durable。
- [Risk] clean reboot smoke 容易受本地 QEMU、ROM、工具链或持久测试盘配置影响。→ Mitigation: 验证记录工具可用性、跳过原因和残余风险；源码级检查作为基础回归，runtime passed 只在实际 marker 达成后记录。
- [Risk] 单核实现可能在未来 SMP 中暴露锁和内存序问题。→ Mitigation: 新增代码遵守既有 SMP preparation 边界，不在本 change 中启用多核执行。

## Migration Plan

1. 扩展或整理 page/buffer cache 的 `sync_device()`/selective write-back API，确保失败不清 dirty、不复用失败 victim。
2. 将 bigfs `fsync`、metadata commit flush、显式 backend sync、cache invalidation/eviction 验证路径全部收束到缓存 write-back API。
3. 增加 syscall、libc wrapper 和 shell builtin 的 bounded `sync()` surface，并保持 errno/error reporting 可观察。
4. 补充源码级测试，检查 ordered commit、dirty 保留、device-scoped sync、用户态 `sync()` dispatch 和不可阻塞上下文边界。
5. 补充默认关闭 persistent writable smoke，覆盖写入同步、clean reboot verify、淘汰后重载、shell/libc `sync`、失败 marker 或 skipped/blocked 记录。
6. 若实现导致 boot 或持久 smoke 阻塞，回退本 change 的源码修改即可恢复到现有 bounded clean-sync 基线；不需要磁盘格式迁移。
