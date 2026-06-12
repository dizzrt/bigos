## Why

`harden-memory-correctness` 已经把页数接口与 physical order 接口初步拆开，但当前代码仍保留 `alloc_pages()`、`alloc_physical_pages()`、`kmem_memory_alloc_pages()` 等旧入口或遗留声明，容易让后续调用方继续混淆“内核虚拟页数”和“buddy order”。现在需要在继续扩展内存管理前，把公开 API、内部 API 和映射语义固定下来，避免新的驱动、KTL 容器或内核组件建立在模糊接口上。

## What Changes

- **BREAKING**: 移除或隐藏旧的 `alloc_pages()` 公开别名，要求内核调用方使用语义明确的 `alloc_kernel_pages(nr_pages, flags)`。
- **BREAKING**: 移除或隐藏 `alloc_physical_pages(order, flags)` 旧别名，内部物理页调用方统一使用 `alloc_physical_order(order, flags)`。
- 清理 `kmem.h` 中未实现或未使用的遗留声明，例如 `kmem_cache_alloc()`、`kmem_memory_alloc_pages()`，避免头文件表达不存在的 API。
- 明确 `kmalloc()` / `operator new` 返回的内核地址必须已经可访问，不要求调用方显式传入 `_GFM_PRE_PAGING`。
- 明确 `alloc_kernel_pages(nr_pages, flags)` 的默认语义：请求内核虚拟页数，是否立即建立 physical backing 由命名清晰的 flag 或后续专用 API 控制；本 change 不引入 demand paging。
- 明确 `_GFM_PRE_PAGING` 只属于内存管理内部/低层页映射策略，不应成为普通 `kmalloc()` 调用方需要理解的约束。
- 增加源码/构建检查，防止新代码继续调用旧 alias 或重新引入页数/order 语义混用。

## Capabilities

### New Capabilities

### Modified Capabilities
- `kernel-memory-correctness`: 收紧 kernel memory API 语义，要求公开虚拟页接口、内部 physical order 接口和 slab/kmalloc 映射语义保持清晰，旧 alias 与遗留声明不得继续暴露。

## Impact

- 受影响子系统：`include/bigos/memory.h`、`kernel/mm/buddy.h`、`kernel/mm/buddy.cc`、`kernel/mm/kmem.h`、`kernel/mm/vmem.cc`、`kernel/mm/slab.cc`，以及可能直接包含这些头文件的内核/KTL 调用方。
- API 影响：`alloc_pages()` 与 `alloc_physical_pages()` 作为旧语义入口会被移除、隐藏或迁移到兼容期外；调用方需要使用 `alloc_kernel_pages()` 和 `alloc_physical_order()`。
- 架构假设：x86_64 long mode、4 KiB 页、现有 `KVMEM_BASE` 内核虚拟分配区、现有 self-mapping 地址公式和 boot-stage PML4 固定物理地址保持不变。
- 内存布局假设：不移动低 2 MiB 保留区、kernel 物理加载基址 `0x1000000`、higher-half kernel base `0xffffffff80000000` 或 `0x2000` PML4。
- 工具链假设：继续使用 xmake 和 `x86_64-elf-*` 交叉工具链构建；Bochs 只作为可用时的启动 smoke test。
- 非目标：不引入 scheduler、IRQ enable、SMP、锁、per-CPU allocator、direct map、用户态地址空间、demand paging、页权限模型、TLB shootdown 或 early allocator。
