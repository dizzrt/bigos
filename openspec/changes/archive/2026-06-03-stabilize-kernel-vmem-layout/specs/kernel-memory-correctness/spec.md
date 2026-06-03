## MODIFIED Requirements

### Requirement: Buddy allocator preserves physical page invariants

早期物理页分配器 SHALL 在初始化、分配和释放后保持 zone free list、allocated list、PageBlock 生命周期和全局页数统计一致。它 MUST 只把 BootInfo 中 normalized type 为 usable 的内存加入可分配集合，并 MUST 保留低地址、kernel 映像和非 usable region。buddy split 过程中如果元数据分配失败，allocator MUST 恢复原始 PageBlock 并保持统计不变。

#### Scenario: 初始化只消费可用内存

- **WHEN** kernel 调用 `init_mem()` 并传入包含 usable 与 reserved memory regions 的 BootInfo
- **THEN** buddy 初始化只把 usable region 中未被低地址和 kernel 映像占用的 4 KiB 对齐页加入 free list

#### Scenario: split 后统计一致

- **WHEN** 调用物理页分配路径请求一个小于可用 PageBlock order 的 order
- **THEN** allocator 拆分较大的 PageBlock，并使全局 free page 计数减少 `1 << requested_order`

#### Scenario: 释放相邻块后安全合并

- **WHEN** 两个同 zone、同 order、物理地址相邻的 PageBlock 被释放
- **THEN** buddy 将它们合并为更高 order 的 PageBlock，且不会访问已销毁的 PageBlock 或 list node

#### Scenario: split 元数据失败恢复原始块

- **WHEN** `Zone::alloc(order)` 拆分较大 PageBlock 时无法分配剩余块所需的 `PageBlock` 或 list node 元数据
- **THEN** allocator 恢复被拆分前的原始 PageBlock、重新放回对应 free list、保持 zone/global free page 统计不变，并向调用方返回分配失败

### Requirement: Kernel virtual memory allocator preserves virtual range invariants

内核虚拟内存分配器 SHALL 按请求页数分配不重叠的 kernel virtual ranges，并 MUST 在分配、释放和合并后保持 free/used list 与 `nr_free_pages_` 一致。`KVMEM_BASE` 管理的区域 MUST 表达 kernel heap/vmalloc-style virtual allocation 区，而不是 direct map。页表映射 MUST 使用正确的 x86_64 四级页表 9 位索引。已映射 kernel virtual pages 在释放时 MUST 清除 PTE 并在当前单核环境下刷新对应 TLB entry。kernel virtual pages 分配接口 MUST 只使用页数语义，physical buddy 分配接口 MUST 只使用 order 语义；实现中不得保留会混淆这两层语义的旧 alias。

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

#### Scenario: KVMEM 区域不是 direct map

- **WHEN** 代码或文档描述 `KVMEM_BASE` 管理的虚拟地址区
- **THEN** 该区域被定义为 kernel virtual allocation 区，分配地址不依赖 `virt = phys + offset` 的 direct-map 关系

#### Scenario: free_pages 清除已映射 PTE

- **WHEN** 调用 `free_pages(addr)` 释放通过 `_GFM_PRE_PAGING` 建立 backing mapping 的 kernel virtual range
- **THEN** VMem 在归还 physical backing 前清除该 range 的 PTE，并对每个被清除的虚拟页执行当前 CPU 的 TLB invalidation

### Requirement: Allocation failures leave allocator state consistent

早期内存管理路径 SHALL 在物理页、虚拟页、slab 元数据、buddy split 元数据或页表页分配失败时保持 allocator 状态一致。失败路径 MUST 不泄漏已插入的 list node，不保留未完成的 used block，不写入指向无效页表页的 descriptor，不残留半完成的 backing PTE。

#### Scenario: 动态 slab 元数据分配失败

- **WHEN** slab 扩容过程中 heap、bitmap、Slab 对象或 list node 任一步分配失败
- **THEN** `kmalloc()` 返回 `nullptr`，且 cache 的 avl/full list 和对象计数不包含半初始化 slab

#### Scenario: 页表页分配失败

- **WHEN** VMem set_paging 需要新页表页但物理页分配失败
- **THEN** 映射操作安全失败，不得写入 present descriptor 指向空地址或无效物理页

#### Scenario: 映射中途失败回滚已写 PTE

- **WHEN** VMem 为一个 MemoryBlock 建立 backing mapping 时在部分 PTE 写入后遇到页表页、backing 记录或其它映射前置条件失败
- **THEN** VMem 清除本次映射已写入的 PTE、刷新对应 TLB entry、释放本次已记录的 physical backing，并将虚拟区间恢复为可再次分配或返回安全失败状态

#### Scenario: buddy split 元数据失败不污染 free list

- **WHEN** buddy split 创建剩余 PageBlock 元数据时失败
- **THEN** 已创建的剩余块节点被撤销，原始块回到原 order free list，且 allocated list 不包含该失败请求的 PageBlock

### Requirement: Memory correctness validation is reproducible

该 change SHALL 提供最小可重复验证路径，通过测试覆盖内存管理不变量。若本地环境缺少 cross toolchain 或 Bochs，验证记录 MUST 明确说明哪些检查无法执行。本 change MUST NOT 通过新增 debug-only allocator 不变量检查函数替代测试覆盖。

#### Scenario: 构建验证

- **WHEN** 开发者完成内存正确性修复
- **THEN** 至少运行项目支持的最窄可用构建命令，并记录成功或失败原因

#### Scenario: boot smoke 验证

- **WHEN** Bochs 和 boot image 环境可用
- **THEN** 开发者运行 kernel boot smoke test，确认 `init_mem()` 后 kernel 能继续到达既有启动输出或等价可观察状态

#### Scenario: boot 资产生成验证

- **WHEN** Bochs runtime smoke 因本机 ROM、交互限制或模拟器配置不可用而不能执行
- **THEN** 开发者至少运行 boot debug 的 no-launch 路径，验证 kernel build、boot build、raw image 和 generated bochsrc 能成功生成，并记录未运行 emulator 的原因

#### Scenario: 不变量测试

- **WHEN** 项目中存在可运行的 allocator 测试或新增了 freestanding-safe 的测试辅助
- **THEN** 测试覆盖 buddy split/merge、slab size class、VMem 分配释放、页表索引边界、页数/order API 使用边界、map/unmap 生命周期和失败回滚边界

#### Scenario: 不新增调试检查函数

- **WHEN** 开发者完成内存正确性修复
- **THEN** change 不新增 `debug_check_buddy()`、`debug_check_vmem()` 或等价 debug-only allocator 扫描函数
