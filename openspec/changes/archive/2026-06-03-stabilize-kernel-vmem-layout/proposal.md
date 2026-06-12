## Why

前两个内存管理 change 已经修复了 buddy/slab/VMem 的主要正确性问题，并把页数接口与 physical order 接口拆开；但当前 VMem 仍缺少明确的地址空间角色、页表映射生命周期和失败回滚约束。

现在需要在继续扩展内核堆、驱动和后续子系统前稳定 kernel virtual memory 布局，避免 `free_pages()` 释放 backing 后残留 PTE、`set_paging()` 失败后留下半完成页表，以及 buddy split 元数据分配失败破坏 allocator 状态。

## What Changes

- 明确 `KVMEM_BASE` 当前区域是 kernel heap/vmalloc-style 虚拟分配区，不是 direct map；本 change 不移动现有 boot/linker/self-mapping 地址。
- 为 VMem 建立显式 map/unmap 语义：成功映射的 kernel virtual pages 在释放时必须清除 PTE，并在当前单核环境下执行必要的 TLB 刷新。
- 加固 `set_paging()` 失败路径：页表页或 backing 元数据分配失败时不得留下指向无效页的 present descriptor，也不得泄漏已分配 physical backing。
- 加固 buddy split 元数据失败路径：拆分大 PageBlock 时如果 `PageBlock` 或 list node 分配失败，allocator 必须恢复原始块和统计，而不是返回半拆分状态。
- 补充源码级/构建/启动资产验证，覆盖 map/unmap、失败回滚、buddy split 元数据失败和既有 API 语义。
- 保留 `free_pages()` 名称并继续约束为 `alloc_kernel_pages()` 的释放入口；不在本 change 中做 API polish rename。
- 不引入 scheduler、IRQ enable、SMP、锁、per-CPU allocator、完整 direct map、用户态地址空间、页权限模型或页表页回收框架。

## Capabilities

### New Capabilities

- 无

### Modified Capabilities

- `kernel-memory-correctness`: 增加 kernel virtual memory layout、映射/取消映射生命周期、TLB 刷新、页表失败回滚和 buddy split 元数据失败一致性要求。

## Impact

- 影响子系统：`kernel/mm` 下的 VMem、buddy physical allocator、slab grow backing 路径和公开 `alloc_kernel_pages()` / `free_pages()` 行为。
- 影响代码：`kernel/mm/vmem.cc`、`kernel/mm/vmem.h`、`kernel/mm/buddy.cc`、`kernel/mm/buddy.h`、`kernel/mm/memdef.h`、`include/bigos/memory.h`、相关源码级测试。
- API 影响：不恢复旧 `alloc_pages()` / `alloc_physical_pages()` alias；可新增内部 helper 表达 map/unmap 和 TLB flush，但不扩大普通调用方可见的 `_GFM_PRE_PAGING` 语义。
- API 命名：不把 `free_pages()` 重命名为 `free_kernel_pages()`；未来若需要命名统一，应放入独立 kernel memory API polish。
- 架构假设：x86_64 四级页表、现有 recursive self-mapping 地址、`0x2000` boot-stage PML4、`0xffff880000000000` kernel virtual allocation base、`0xffffffff80000000` higher-half kernel base 均保持不变。
- 验证假设：以 `xmake`、`uv run pytest`、`openspec validate --all`、`uv run python tools/boot_debug.py run --no-launch` 为基础；Bochs runtime smoke 仅在本机 ROM/模拟器配置可用且可控时运行并记录结果。
