## Why

当前 slab/kmalloc 已能处理基础 size class 和动态扩容，但 slab 生命周期仍不完整：空 slab 不回收，大对象没有明确路径，debug 检测和统计可观测性不足。`introduce-early-memory-metadata-arena` 已将 buddy 初始化期 `PageBlock` 和 list node 元数据从普通 slab/kmalloc 动态增长中解耦；本 change 在该 baseline 上继续完善运行期 slab allocator，使内核堆能支撑更长时间运行和后续子系统接入。

## What Changes

- 为非永久 slab 增加空 slab 回收策略，在 cache 保留必要余量的前提下归还 backing pages 和元数据。
- 为超过静态 small-object size class 的 `kmalloc()` 增加 page-backed large allocation 路径，明确释放策略和 header 标识。
- 保持 early metadata arena 仅服务 buddy bootstrap；slab metadata、large allocation metadata 和运行期 buddy split 仍使用普通 allocator 路径。
- 完善 `GFM_PERFECT_FIT` / `_GFM_NEW_CACHE_TO_PFIT` 的处理：保留精确匹配语义，当前阶段禁用自动动态 cache 创建。
- 增加 slab debug guard，可选覆盖 double free、非法指针、对象边界和 poison pattern。
- 增加 slab/cache 统计接口，输出 size class、slab 数、对象数、free/full 状态和 large allocation 统计。
- 增加源码级与 runtime self-test，覆盖 small object、large object、空 slab 回收和失败回滚。
- 不引入 scheduler、IRQ enable、SMP/per-CPU cache、NUMA、用户态 heap 或完整 kmem_cache_create API。

## Capabilities

### New Capabilities
- `slab-allocator-lifecycle`: 覆盖 slab 空闲回收、大对象分配、debug guard、统计和测试要求。

### Modified Capabilities
- `kernel-memory-correctness`: 将 slab 正确性要求扩展到生命周期回收、大对象路径和 debug/统计可观测性。

## Impact

- 影响子系统：`kernel/mm/slab.*`、`kernel/mm/kmem.*`、`kernel/mm/vmem.*`、`include/bigos/memory.h`，以及 `cpp/libsupc++/new.cc` 间接受益。
- 架构假设：当前仍为单核、关中断早期内核路径，不设计并发分配安全；`kmalloc/new` 成功时必须返回已映射可访问内核虚拟地址。
- 内存布局假设：继续使用 kernel heap/vmalloc-style `KVMEM_BASE`，不引入 direct map 或移动页表 self-mapping 布局。
- Bootstrap 假设：buddy 初始化元数据由 early metadata arena 提供，当前 change 不改变 arena 来源、容量、生命周期或 BootInfo memory map ABI。
- API 影响：`kmalloc/free` 行为增强但语义保持兼容；`_GFM_NEW_CACHE_TO_PFIT` 自动 cache 创建语义在当前阶段明确不可用，并通过 spec 和源码级测试禁止新调用点误用。
