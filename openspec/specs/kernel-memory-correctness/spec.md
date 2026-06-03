## Purpose

Define memory correctness requirements for BigOS early kernel memory management, including buddy physical page allocation, slab/kmalloc size classes, kernel virtual memory allocation, page-table mapping, allocation failure rollback, and reproducible validation.

## Requirements

### Requirement: Buddy allocator preserves physical page invariants

早期物理页分配器 SHALL 在初始化、分配和释放后保持 zone free list、allocated list、PageBlock 生命周期和全局页数统计一致。它 MUST 只把 BootInfo 中 normalized type 为 usable 的内存加入可分配集合，并 MUST 保留低地址、kernel 映像和非 usable region。

#### Scenario: 初始化只消费可用内存

- **WHEN** kernel 调用 `init_mem()` 并传入包含 usable 与 reserved memory regions 的 BootInfo
- **THEN** buddy 初始化只把 usable region 中未被低地址和 kernel 映像占用的 4 KiB 对齐页加入 free list

#### Scenario: split 后统计一致

- **WHEN** 调用物理页分配路径请求一个小于可用 PageBlock order 的 order
- **THEN** allocator 拆分较大的 PageBlock，并使全局 free page 计数减少 `1 << requested_order`

#### Scenario: 释放相邻块后安全合并

- **WHEN** 两个同 zone、同 order、物理地址相邻的 PageBlock 被释放
- **THEN** buddy 将它们合并为更高 order 的 PageBlock，且不会访问已销毁的 PageBlock 或 list node

### Requirement: Slab allocator provides correct kmalloc size classes

slab/kmalloc allocator SHALL 为声明的静态 size class 建立匹配对象大小的 cache，并 MUST 在支持范围内为 `kmalloc(size)` 和全局 `operator new` 返回可直接访问的内核地址。分配失败时 MUST 返回 `nullptr` 或遵循现有 new 失败策略，不得返回半初始化或未映射对象。

#### Scenario: kmalloc 选择足够大的 size class

- **WHEN** 调用 `kmalloc(size)` 且 `size` 落在已声明的静态 size class 范围内
- **THEN** CacheChain 选择对象大小等于或大于 `size` 的最小可用 cache，除非调用方显式要求 perfect fit

#### Scenario: 静态 cache 对象大小与名称一致

- **WHEN** 内存初始化注册 `16B`、`32B`、`64B`、`128B`、`256B`、`512B`、`1024B` 和 `2048B` cache
- **THEN** 每个 cache 的对象大小与其名称表达的容量一致

#### Scenario: slab 扩容返回可访问地址

- **WHEN** 一个 cache 的可用 slab 耗尽并需要动态扩容
- **THEN** 新 slab 的 heap backing 已经映射到当前内核页表，调用方可以立即读写 `kmalloc()` 返回的对象地址

#### Scenario: 释放对象更新 slab 状态

- **WHEN** 调用 `free(ptr)` 释放由 slab 分配的对象
- **THEN** 对象位图被重置，cache free object 计数增加，full slab 在释放后重新进入可分配列表

### Requirement: Memory API exposes explicit allocation semantics

BigOS early kernel memory API SHALL expose allocation entry points whose names identify the allocation layer and unit. Public kernel virtual page allocation MUST use page-count semantics through `alloc_kernel_pages(nr_pages, flags)`. Internal physical page allocation MUST use buddy-order semantics through `alloc_physical_order(order, flags)`. Legacy aliases that obscure this distinction MUST NOT remain declared or defined.

#### Scenario: 公开虚拟页分配入口使用页数语义

- **WHEN** kernel 代码需要分配 `nr_pages` 个内核虚拟页
- **THEN** 调用方使用 `alloc_kernel_pages(nr_pages, flags)`，且该参数表达页数而不是 buddy order

#### Scenario: 内部物理页分配入口使用 order 语义

- **WHEN** VMem 或 buddy 内部代码需要分配 `2^order` 个连续物理页
- **THEN** 调用方使用 `alloc_physical_order(order, flags)`，且该参数表达 buddy order 而不是页数

#### Scenario: 旧 alias 不再暴露

- **WHEN** 开发者搜索公开头文件和 `src/mm` 实现
- **THEN** 不存在 `alloc_pages()`、`alloc_physical_pages()`、`free_physical_pages()` 或未实现的 `kmem_memory_alloc_pages()` 入口供新调用点使用

#### Scenario: kmalloc 调用方不需要映射 flag

- **WHEN** 普通内核代码调用 `kmalloc(size)` 或全局 `operator new(size)`
- **THEN** 返回地址在成功时已经可直接访问，调用方不需要传入 `_GFM_PRE_PAGING` 来保证对象可读写

#### Scenario: free_pages 配对 kernel virtual pages

- **WHEN** 调用方释放由 `alloc_kernel_pages(nr_pages, flags)` 返回的地址
- **THEN** 调用方使用 `free_pages(ptr)`，该释放路径处理 VMem 区间和已建立的 physical backing

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

### Requirement: Allocation failures leave allocator state consistent

早期内存管理路径 SHALL 在物理页、虚拟页、slab 元数据或页表页分配失败时保持 allocator 状态一致。失败路径 MUST 不泄漏已插入的 list node，不保留未完成的 used block，不写入指向无效页表页的 descriptor。

#### Scenario: 动态 slab 元数据分配失败

- **WHEN** slab 扩容过程中 heap、bitmap、Slab 对象或 list node 任一步分配失败
- **THEN** `kmalloc()` 返回 `nullptr`，且 cache 的 avl/full list 和对象计数不包含半初始化 slab

#### Scenario: 页表页分配失败

- **WHEN** VMem set_paging 需要新页表页但物理页分配失败
- **THEN** 映射操作安全失败，不得写入 present descriptor 指向空地址或无效物理页

### Requirement: Memory correctness validation is reproducible

该 change SHALL 提供最小可重复验证路径，通过测试覆盖内存管理不变量。若本地环境缺少 cross toolchain 或 Bochs，验证记录 MUST 明确说明哪些检查无法执行。本 change MUST NOT 通过新增 debug-only allocator 不变量检查函数替代测试覆盖。

#### Scenario: 构建验证

- **WHEN** 开发者完成内存正确性修复
- **THEN** 至少运行项目支持的最窄可用构建命令，并记录成功或失败原因

#### Scenario: boot smoke 验证

- **WHEN** Bochs 和 boot image 环境可用
- **THEN** 开发者运行 kernel boot smoke test，确认 `init_mem()` 后 kernel 能继续到达既有启动输出或等价可观察状态

#### Scenario: 不变量测试

- **WHEN** 项目中存在可运行的 allocator 测试或新增了 freestanding-safe 的测试辅助
- **THEN** 测试覆盖 buddy split/merge、slab size class、VMem 分配释放、页表索引边界以及页数/order API 使用边界

#### Scenario: 不新增调试检查函数

- **WHEN** 开发者完成内存正确性修复
- **THEN** change 不新增 `debug_check_buddy()`、`debug_check_vmem()` 或等价 debug-only allocator 扫描函数
