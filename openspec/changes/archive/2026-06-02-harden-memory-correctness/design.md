## Context

BigOS 当前在 `kernel()` 早期调用 `bigos::init_mem()`，初始化顺序为静态 slab/cache、物理 buddy、内核虚拟内存池。这个分层方向正确，但实现中存在若干会破坏早期内核堆不变量的问题：

- buddy 的前驱合并、合并后节点生命周期和统计更新不可靠。
- slab 的静态 size class 定义与名称不一致，动态扩容路径对失败和映射语义处理不足。
- VMem 的页表索引、first-fit 选择、空闲页计数和释放合并存在错误。
- `kmalloc/new` 调用方预期获得可直接访问的内核地址，但扩容时可能只拿到未映射的虚拟区间。

本设计只覆盖单核、关中断、无 scheduler/IRQ enable/SMP 的早期内存管理正确性加固。它不改变 boot-stage 固定地址、不改变 linker higher-half 地址、不重新设计 self-mapping 布局，只修复现有路径中会导致错误分配或页表损坏的问题。

当前数据流保持为：

```text
BootInfo memory map
  -> init_buddy()
     -> Zone free_area_[order]
        -> alloc_physical_pages(order)
           -> VMem paging backing

init_vmem()
  -> KVMEM_BASE virtual free_area_
     -> alloc_kernel_pages(nr_pages)
        -> optional/currently-required page table mapping

init_cache()
  -> static Cache/Slab
     -> kmalloc(size)
        -> CacheChain -> Cache -> Slab object
        -> slab grow via mapped kernel pages
```

## Goals / Non-Goals

**Goals:**

- 保证 buddy free list 按地址有序、split/merge 后 PageBlock 节点生命周期清晰，`gNrFreePages` 与 zone 统计一致。
- 保证 slab 静态 cache 覆盖声明的 size class，`kmalloc(size)` 在支持范围内选择正确 cache 并返回可直接访问的内核地址。
- 保证 VMem 按请求页数选择足够大的 free block，分配/释放后维护 `nr_free_pages_`，释放时只合并相邻区间。
- 保证页表索引宏覆盖 x86_64 四级页表的 9 位索引，并在页表页分配失败时安全失败。
- 在本 change 中拆分页数与 buddy order 语义，提供明确的 kernel virtual pages 与 physical order allocation 接口边界。
- 通过测试覆盖 allocator 不变量，包括构建检查、可运行时的 boot smoke test，以及必要的 deterministic 单元/辅助测试。

**Non-Goals:**

- 不引入 scheduler、IRQ enable、SMP、锁、per-CPU cache 或中断上下文分配策略。
- 不实现完整 direct map、用户态地址空间、页权限模型、TLB shootdown、换页或页表页回收策略。
- 不移动 `0x2000` boot-stage PML4、低地址 handoff 区、kernel 物理加载基址或 higher-half kernel base。
- 不添加 debug-only allocator 不变量检查函数；不变量只通过测试、构建和 boot smoke 验证。
- 不引入 early allocator，也不主动大幅增加静态 slab 容量；若现有容量导致验证失败，只记录风险，不在本 change 中用容量堆叠规避 bootstrap 设计问题。
- 不把 slab 做成完整通用缓存框架；只修复当前可用路径的正确性。

## Decisions

### Decision: 优先修复现有分层，不做架构替换

本 change 保留 `buddy -> VMem -> slab/kmalloc` 的当前结构，只修复不变量和边界处理。

替代方案是引入 early bump allocator、重写 VMem 或重命名内存 API。该方案能更系统地解决 bootstrap 与语义问题，但会扩大影响面，并与“harden correctness”目标不匹配。因此这些工作作为后续 change 候选。

### Decision: `kmalloc/new` 路径必须返回已映射地址

`kmalloc()` 和全局 `operator new` 的调用方不应感知页表映射细节。slab 动态扩容时，新 slab heap 必须是可直接读写的内核虚拟地址；若无法映射，分配失败并返回 `nullptr`，不得把未映射地址暴露给调用方。

替代方案是保留 `_GFM_PRE_PAGING` 由调用方显式传入，但这会让普通 `kmalloc()` 的安全性依赖调用方知识，容易在 KTL 容器或元数据分配中产生隐藏 page fault。

### Decision: 本 change 立即拆分页数与 physical order API

现有 `alloc_pages(uint32_t __pages, gfm_t __gfm)` 与 slab 中的 `buddy_order_` 调用存在语义错配。本 change 将立即拆分接口：kernel virtual pages 分配接口按 `nr_pages` 表达数量，physical buddy 分配接口按 `order` 表达 `2^order` 个物理页。公开或内部命名在实施时应体现这两个层级，例如 `alloc_kernel_pages(nr_pages, flags)` 与 `alloc_physical_order(order, flags)`。

替代方案是只修 slab 调用点、把 API 拆分留到后续 change。该方案改动更小，但会继续保留最容易再次出错的语义债务；因此本 change 直接完成拆分，并同步迁移当前调用方。

### Decision: VMem 使用 first-fit，并维护页数统计

VMem 分配必须遍历 `free_area_`，选择 `nr_pages >= request` 的第一个区间。拆分后原节点进入 `used_area_`，剩余节点进入 `free_area_`，并扣减 `nr_free_pages_`。释放时从 `used_area_` 移除，释放 physical backing，清空 `physical_area`，插回 `free_area_` 并只和相邻区间合并。

替代方案是最佳适配或按 order 管理虚拟区间。当前虚拟区间数量很少，first-fit 更简单，也足以覆盖早期内核堆。

### Decision: buddy merge 必须以节点所有权为核心

buddy 的 `__base_free()` 负责把节点插入对应 order free list 后触发合并。合并成功后必须明确保留哪个 PageBlock/node、销毁哪个 PageBlock/node，并避免调用方继续访问已销毁节点。新 region 纳入统计时，应基于释放前或合并后保留的页数语义更新总页数，不能依赖可能被释放的指针。

替代方案是让 `merge()` 返回保留节点并逐层循环合并。这更清晰，实施时可采用；关键约束是调用方不能使用已被 merge 删除的 node。

### Decision: 页表索引修复为 x86_64 9 位索引

PML4、PDPT、PD、PT 四级索引都必须使用 9 位掩码。PT index 应覆盖虚拟地址 bit 12..20。页表项写入仍使用现有默认属性，属性模型不在本 change 扩展。

替代方案是改成移位后 `& 0x1ff` 的 helper 函数，能减少掩码错误。实施时可以采用宏或 `constexpr` helper，但不改变 self-mapping 地址布局。

### Decision: 失败路径以安全失败为主

早期 allocator 遇到无法满足的动态分配时，应返回 `nullptr` 或保持现有 fatal-halt 行为，不能部分修改链表或页表后继续。对于 `init_buddy()` 解析不到 usable region 的既有 halt 行为保留；对于普通分配失败，优先返回 `nullptr`。

替代方案是引入统一 `panic()`。当前项目还没有完整 panic/console 策略，本 change 不强制引入新错误处理框架。

### Decision: 不添加 debug-only 不变量检查函数

allocator 不变量只通过测试、构建检查和 boot smoke test 覆盖。本 change 不添加 `debug_check_buddy()`、`debug_check_vmem()` 或类似内部调试扫描函数，避免增加内核代码体积、运行时路径和额外维护面。

替代方案是添加 `#ifdef BIGOS_MM_DEBUG` 控制的内部检查函数。该方案有助于定位问题，但当前 change 更关注修复生产路径和可重复测试，调试函数可在后续专门测试/诊断 change 中再评估。

### Decision: 不扩张 bootstrap 范围

本 change 不主动大幅增加静态 slab 容量，也不引入 early allocator。静态 slab 只修正 size class 正确性；若当前容量在验证中暴露不足，记录为后续 bootstrap/early metadata allocator 工作，而不是通过堆叠静态容量掩盖初始化阶段依赖问题。

替代方案是增加更多静态 slab 或引入 early bump allocator。前者只能缓解症状，后者会扩大架构范围；两者均不属于本次 correctness hardening 的核心目标。

## Risks / Trade-offs

- [Risk] 修复 slab 扩容时默认映射可能增加早期物理页消耗 -> Mitigation: 仅在需要新 slab 时分配 backing，并保留失败返回路径。
- [Risk] 修正 buddy merge 可能改变现有 free list 形态，暴露之前被隐藏的统计错误 -> Mitigation: 用测试覆盖 free page 统计和 order list 不变量，不加入运行时调试扫描函数。
- [Risk] VMem first-fit 与释放合并修复可能触发当前未覆盖的页表路径 -> Mitigation: 优先添加窄路径测试或 boot smoke，若 Bochs 不可用则记录验证限制。
- [Risk] 立即拆分页分配 API 会扩大调用方迁移范围 -> Mitigation: 仅迁移当前代码库内调用点，保持语义分层清晰，并在任务中单独审查 API 使用点。
- [Risk] 不引入锁意味着 allocator 仍不能在并发或 IRQ 上下文安全使用 -> Mitigation: 在文档和任务中明确当前只保证单核、关中断路径。
- [Risk] 不增加静态 slab 容量可能在复杂内存图下暴露 bootstrap 元数据容量不足 -> Mitigation: 验证中记录容量风险，后续用 early metadata allocator 设计解决。

## Migration Plan

1. 先修复不会改变外部行为的局部 bug：slab size class、PT index、空指针检查。
2. 再修复 buddy 与 VMem 的链表生命周期和统计不变量。
3. 拆分页数分配 API 与 physical order 分配 API，并迁移现有调用点。
4. 调整 slab 动态扩容路径，确保新增 slab heap 可访问，失败时不留下半初始化对象。
5. 运行最小构建检查；如工具链和 Bochs 可用，执行 boot smoke test。
6. 若验证失败，回退本 change 中对应 allocator 修复，不回退无关 boot 或驱动代码。

## Resolved Decisions

- allocator 不变量只通过测试覆盖，不添加 debug-only 不变量检查函数。
- 本 change 立即拆分 `alloc_pages()` 的页数语义与 physical buddy order 语义。
- 本 change 不做大幅静态 slab 扩容，也不引入 early allocator。
