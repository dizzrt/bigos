## ADDED Requirements

### Requirement: Memory API exposes explicit allocation semantics
BigOS early kernel memory API SHALL expose allocation entry points whose names identify the allocation layer and unit. Public kernel virtual page allocation MUST use page-count semantics through `alloc_kernel_pages(nr_pages, flags)`. Internal physical page allocation MUST use buddy-order semantics through `alloc_physical_order(order, flags)`. Legacy aliases that obscure this distinction MUST NOT remain declared or defined.

#### Scenario: 公开虚拟页分配入口使用页数语义
- **WHEN** kernel 代码需要分配 `nr_pages` 个内核虚拟页
- **THEN** 调用方使用 `alloc_kernel_pages(nr_pages, flags)`，且该参数表达页数而不是 buddy order

#### Scenario: 内部物理页分配入口使用 order 语义
- **WHEN** VMem 或 buddy 内部代码需要分配 `2^order` 个连续物理页
- **THEN** 调用方使用 `alloc_physical_order(order, flags)`，且该参数表达 buddy order 而不是页数

#### Scenario: 旧 alias 不再暴露
- **WHEN** 开发者搜索公开头文件和 `kernel/mm` 实现
- **THEN** 不存在 `alloc_pages()`、`alloc_physical_pages()`、`free_physical_pages()` 或未实现的 `kmem_memory_alloc_pages()` 入口供新调用点使用

#### Scenario: kmalloc 调用方不需要映射 flag
- **WHEN** 普通内核代码调用 `kmalloc(size)` 或全局 `operator new(size)`
- **THEN** 返回地址在成功时已经可直接访问，调用方不需要传入 `_GFM_PRE_PAGING` 来保证对象可读写

#### Scenario: free_pages 配对 kernel virtual pages
- **WHEN** 调用方释放由 `alloc_kernel_pages(nr_pages, flags)` 返回的地址
- **THEN** 调用方使用 `free_pages(ptr)`，该释放路径处理 VMem 区间和已建立的 physical backing

## MODIFIED Requirements

### Requirement: Kernel virtual memory allocator preserves virtual range invariants
内核虚拟内存分配器 SHALL 按请求页数分配不重叠的 kernel virtual ranges，并 MUST 在分配、释放和合并后保持 free/used list 与 `nr_free_pages_` 一致。页表映射 MUST 使用正确的 x86_64 四级页表 9 位索引。kernel virtual pages 分配接口 MUST 只使用页数语义，physical buddy 分配接口 MUST 只使用 order 语义；实现中不得保留会混淆这两层语义的旧 alias。

#### Scenario: first-fit 选择足够大的虚拟区间
- **WHEN** 调用 kernel virtual pages 分配接口请求 `nr_pages` 个页
- **THEN** VMem 从 free list 中选择第一个页数不小于 `nr_pages` 的 MemoryBlock，而不是无条件使用 free list 头节点

#### Scenario: 虚拟页分配更新计数
- **WHEN** VMem 成功分配 `nr_pages` 个虚拟页
- **THEN** `nr_free_pages_` 减少 `nr_pages`，且该区间从 free list 移入 used list

#### Scenario: 虚拟页释放恢复计数并合并相邻区间
- **WHEN** 调用 `free_pages(addr)` 释放一个已分配的虚拟区间
- **THEN** VMem 释放该区间的 physical backing，清空 physical_area，将区间插回 free list，恢复 `nr_free_pages_`，并只合并地址相邻的区间

#### Scenario: 页表索引覆盖 512 项
- **WHEN** VMem 为连续超过 32 页的虚拟地址设置页表映射
- **THEN** PT index 覆盖 0 到 511 的 9 位范围，不会因掩码错误在 32 页内重复覆盖同一批 PTE

#### Scenario: 页数与 order 接口拆分
- **WHEN** slab 扩容或页表映射需要分配 backing memory
- **THEN** 调用方使用 kernel virtual pages 接口传递页数，使用 physical buddy 接口传递 order，不得把 buddy order 直接传给页数接口

#### Scenario: 旧页分配名称不能继续表达混合语义
- **WHEN** 仓库内源码包含内存分配调用点
- **THEN** 分配调用点不得使用 `alloc_pages()` 或 `alloc_physical_pages()` 这类无法从名称判断层级和单位的旧入口
