## Context

slab/kmalloc 当前已经具备静态 size class、动态 slab 扩容和 `new/delete` 接入，但生命周期能力仍处于早期阶段：动态 slab 空闲后不回收，大对象请求没有明确支持路径，debug 检测和统计输出不足。这会限制后续 kernel 子系统长期运行和内存泄漏定位。

`introduce-early-memory-metadata-arena` 已完成并归档，buddy 初始化期 `PageBlock` 和 intrusive list node 元数据不再依赖普通 slab/kmalloc 动态扩容。本设计在该 baseline 上推进 slab allocator 生命周期：early metadata arena 仍只服务 buddy bootstrap，运行期 buddy split、动态 slab 元数据和 large allocation 元数据继续走普通 allocator 路径。

本设计不引入并发 allocator 或用户态 heap。

## Goals / Non-Goals

**Goals:**

- 为非永久 slab 增加安全空闲回收策略。
- 为超过 small-object cache 的 `kmalloc()` 请求提供 page-backed large allocation 路径。
- 明确 `GFM_PERFECT_FIT` / `_GFM_NEW_CACHE_TO_PFIT` 的长期语义。
- 增加 debug guard 和统计输出，便于 runtime self-test 与人工诊断。
- 保持 `kmalloc/free` 成功时返回已映射可访问内核虚拟地址。

**Non-Goals:**

- 不引入 scheduler/IRQ/SMP 并发安全、per-CPU cache 或锁。
- 不实现完整 Linux 风格 `kmem_cache_create()` API。
- 不引入用户态 heap、process address space 或 page fault lazy allocation。
- 不改变 `KVMEM_BASE` 语义，不引入 direct map。
- 不改变 early metadata arena 的来源、容量、生命周期，也不把它作为 `kmalloc()`、large allocation 或 slab metadata 的通用后备。

## Decisions

### Decision: 空 slab 回收仅针对动态非永久 slab

静态 `SLAB_PERMANENT` slab 是 bootstrap 的一部分，不应被回收。动态 slab 当所有对象空闲且 cache 保留余量满足策略时，可以释放 bitmap、Slab 元数据、list node 和 backing pages。buddy 初始化期 arena-backed metadata 不属于 slab 回收对象，且不会被普通 `free()` 或 `free_pages()` 回收。

替代方案是任何空 slab 都立即回收。该方案会导致频繁分配释放抖动，也可能破坏 bootstrap 静态 slab 假设。

### Decision: small-object 上限由 `CACHE_MAX_OBJ_SIZE` 表达

最大 small-object size class 继续以 `CACHE_MAX_OBJ_SIZE` 表达，当前值保持 2048B。静态 cache 注册、source-level 测试和 large allocation fallback 都应引用或验证该上限，避免文档写死 2048B 后与 `memdef.h` 常量漂移。

替代方案是继续在多个文档和测试中手写 2048B。该方案短期简单，但会让后续调整 size class 时遗漏 large allocation 边界和测试断言。

### Decision: 回收策略保留每个 cache 至少一个可用 slab

第一版 reclaim policy 使用简单的“每个 cache 至少保留一个可用 slab”规则：当动态 slab 完全空闲时，只有在同一 cache 中仍存在其它可分配 slab 时才回收它。该策略比按对象数或页数阈值更容易审查，也能避免刚释放最后一个可用 slab 后下一次分配立即重新扩容。

替代方案是按对象数、页数或时间窗口保留。该方案更精细，但当前内核尚无 scheduler/timer/per-CPU cache，复杂阈值会扩大实现和验证范围。

### Decision: 大对象走 page-backed allocation

超过最大 small-object size class 的 `kmalloc(size)` 应分配足够页数的 kernel virtual range，并用专门 header 标记为 large allocation。`free()` 通过 header 区分 slab object 和 large allocation。large allocation 不使用 early metadata arena；其 header 和运行期元数据必须由 page-backed range 本身或普通 allocator 管理。

替代方案是继续返回 `nullptr`。该方案简单，但会让后续子系统在稍大 buffer 上反复失败。

### Decision: debug guard 可编译期开关控制

double free、非法指针、poison pattern 和边界检测对 bring-up 很有价值，但不应强制成为最小 runtime 成本。通过 `BIGOS_SLAB_DEBUG` 或相邻配置控制。

替代方案是始终启用所有 debug guard。该方案更安全但会增加代码路径和性能成本。

### Decision: 对未完成 flag 做明确收敛

`GFM_PERFECT_FIT` 保留为 size class 精确匹配约束；`_GFM_NEW_CACHE_TO_PFIT` 在当前阶段明确禁用或从公开组合语义中移除。若没有已存在的精确匹配 cache，allocator 应确定性失败或走明确的大对象路径，不应自动创建新 cache。

替代方案是实现动态 perfect-fit cache 创建。该方案需要补充 cache 元数据生命周期、排序插入、失败回滚和最终释放策略，接近完整 `kmem_cache_create()` 的一部分，不适合作为当前 slab lifecycle 成熟化的第一版范围。

## Risks / Trade-offs

- Large allocation header 可能与 slab header 混淆 -> 使用明确 magic/type 字段，并在 `free()` 中分支检查。
- 空 slab 回收可能释放仍在 list 中的节点 -> 回收顺序必须先从 cache list 摘除，再释放对象和 backing。
- Slab 生命周期改动可能影响运行期 buddy split 元数据分配 -> self-test 应覆盖 `init_mem()` 之后的 normal allocator-backed split 路径，确认不会回退依赖 early arena。
- Debug poison 可能影响未初始化内存假设 -> 仅对释放对象和 debug build 启用，并在文档中说明。
- 动态 cache 创建可能扩大复杂度 -> 当前阶段禁用 `_GFM_NEW_CACHE_TO_PFIT` 并让 spec/source-level tests 明确不支持。

## Migration Plan

1. 增加 slab/cache 统计与 debug 可观测性，为后续生命周期变更提供验证信号。
2. 实现 large allocation header 和 page-backed 路径，确保 `free()` 能正确回收。
3. 实现非永久空 slab 回收策略，保持 cache 至少有配置的可用余量。
4. 禁用 `_GFM_NEW_CACHE_TO_PFIT` 自动 cache 创建语义，并用源码级测试禁止未实现 TODO 回归。
5. 扩展 runtime self-test 覆盖 small/large/reclaim/debug 路径，并复用既有 memory self-test marker 验证 `init_mem()` 后 allocator 路径仍可用。
