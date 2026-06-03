## Context

BigOS 当前内存管理已经形成 `buddy -> VMem -> slab/kmalloc` 分层。`harden-memory-correctness` 修复了 size class、VMem first-fit、页数/order API 和若干链表生命周期问题；`clarify-kernel-memory-api` 删除了旧 alias，并明确 `kmalloc/new` 成功时返回可直接访问的内核地址。

剩余问题集中在 VMem 与页表生命周期：

- `KVMEM_BASE` 区域实际承担 kernel heap/vmalloc-style 虚拟区间职责，但代码和文档尚未明确它不是 direct map。
- `alloc_kernel_pages(..., _GFM_PRE_PAGING)` 能建立 PTE，但 `free_pages()` 释放 physical backing 后没有清除 PTE 或刷新 TLB。
- `set_paging()` 中途失败时可能已经写入部分页表项或分配页表页，外层只能回收 `physical_area` backing，不能回滚页表状态。
- buddy split 过程中 `new PageBlock()` 或 list node 分配失败时缺少原子恢复策略。

本 change 仍限定在单核、关中断、早期内核路径，不引入 scheduler、IRQ enable、SMP 或用户态地址空间。

当前目标数据流：

```text
alloc_kernel_pages(nr_pages, flags)
  -> VMem::__alloc_pages(nr_pages)
     -> used_area_ owns virtual range
  -> if mapped:
       allocate physical backing by alloc_physical_order(order)
       record backing in MemoryBlock::physical_area
       map_kernel_range(mblk)
          -> allocate page-table pages as needed
          -> write PTEs only after prerequisites are valid

free_pages(ptr)
  -> find MemoryBlock in used_area_
  -> unmap_kernel_range(mblk)
       -> clear mapped PTEs
       -> invalidate affected virtual pages
  -> free physical backing by free_physical_order()
  -> clear physical_area nodes
  -> move range back to free_area_ and merge neighbors
```

## Goals / Non-Goals

**Goals:**

- 明确 `KVMEM_BASE` 是 kernel virtual allocation 区，服务 `alloc_kernel_pages()`、slab grow 和未来 vmalloc-style 用途。
- 为已映射 kernel virtual pages 实现成对 map/unmap 生命周期，`free_pages()` 必须清除 PTE 并处理当前 CPU 的 TLB 一致性。
- 使 `set_paging()` 或等价 map helper 具备失败回滚能力，失败时不留下无效 present descriptor、半映射 range 或泄漏 backing。
- 使 buddy split 元数据分配失败时恢复原始 free block 和统计，返回失败而不是半拆分状态。
- 保持现有 boot 固定地址、higher-half kernel base、`KERNEL_PML4_ADDR` 和 self-mapping 地址公式不变。
- 通过源码级测试、构建、OpenSpec 校验和 boot image 资产生成验证第三阶段变更。

**Non-Goals:**

- 不实现完整 direct map，也不把 `KVMEM_BASE` 改造成 `phys + offset` 的 direct-map 区。
- 不引入用户态地址空间、进程页表、copy-on-write、demand paging、swap 或页权限模型扩展。
- 不实现页表页回收框架；本 change 可以记录页表页分配和回滚，但不要求释放空 PT/PD/PDPT 页。
- 不引入 scheduler、IRQ enable、SMP、锁、per-CPU slab 或 TLB shootdown。
- 不移动 `0x2000` boot-stage PML4、低 2 MiB 保留区、kernel load base、higher-half kernel base 或现有 recursive self-mapping window。
- 不恢复 `alloc_pages()`、`alloc_physical_pages()` 或其它旧 API alias。

## Decisions

### Decision: 将 `KVMEM_BASE` 定义为 kernel virtual allocation 区

当前 `VMem` 从 `0xffff880000000000` 分配虚拟区间，再按需建立 backing mapping。这不是 direct map，因为返回地址和 physical backing 没有固定 `virt = phys + offset` 关系。

本 change 只通过注释、命名和文档固定语义；实现中不移动 `KVMEM_BASE`。后续如果需要 direct map，应新增独立地址区和 spec，而不是复用当前 heap/vmalloc 区。

替代方案是立即引入 direct map。该方案会影响所有物理页访问、页表属性和地址布局评审，超出第三阶段稳定 VMem 生命周期的范围。

### Decision: 拆出内部 map/unmap helper

`VMem::set_paging()` 应演进为内部映射 helper，职责是把 `MemoryBlock::physical_area` 映射到 `MemoryBlock::base` 开始的虚拟范围。释放路径应新增对应 unmap helper，按 `MemoryBlock` 中记录的页数清除 PTE。

建议内部形态：

```text
bool map_kernel_range(MemoryBlock *mblk) noexcept
void unmap_kernel_range(MemoryBlock *mblk) noexcept
void flush_kernel_tlb_page(uint64_t vaddr) noexcept
```

普通调用方继续只使用 `alloc_kernel_pages()` 和 `free_pages()`，不直接接触 map/unmap helper。

替代方案是在 `free_pages()` 中内联清 PTE。该方案短期更少代码，但会让页表写入、清除和失败回滚逻辑分散，后续难以测试。

### Decision: `free_pages()` 对已映射 range 必须 unmap 再释放 backing

释放顺序必须避免 use-after-free 物理页仍被旧虚拟地址引用：

```text
used_area_ remove
  -> clear PTEs for mblk range
  -> invlpg each cleared virtual page
  -> free_physical_order() backing
  -> clear physical_area nodes
  -> free_area_ insert and merge
```

在当前单核、关中断假设下，逐页 `invlpg` 足够；不需要跨 CPU TLB shootdown。若释放未映射 range，unmap helper 必须安全 no-op。

替代方案是重载 CR3 刷新整个 TLB。它实现简单但影响更大，也不利于后续精确 TLB 管理；本 change 优先采用 `invlpg`。

### Decision: map 失败以“可回滚事务”处理

映射 helper 不应先写入 present descriptor 再发现后续资源不足。推荐策略：

- 分配页表页失败时立即返回 false，不写入指向 0 或无效页的 descriptor。
- 记录本次新建的页表页和已写入 PTE 的虚拟页范围。
- 如果 backing PTE 写入过程中失败，清除本次已写入的 PTE 并刷新对应 TLB。
- 对已经存在的上级页表不做释放；对本次新分配但尚未安全接入的页表页，需要归还 physical buddy。
- 如果上级 descriptor 已经接入但后续失败，至少清零该 descriptor 并刷新相关虚拟页，避免 dangling present entry；完整空页表页回收留到后续。

替代方案是先预分配所有页表页再一次性写入。该方案更原子，但需要先扫描并准确计算缺失层级；当前 self-mapping helper 已经按页推进，增量记录回滚更贴近现有代码。

### Decision: buddy split 元数据失败必须恢复原始块

`Zone::alloc(order)` 从较大 block split 时，会为剩余块创建多个 `PageBlock` 和 list node。任一步元数据分配失败时，应：

- 删除本次已经创建并插入 free list 的剩余块。
- 恢复原始 `pblk` 的 `base/len/order/flags/zone`。
- 将原始 node 重新插回原 order free list。
- 不扣减 `nr_free_pages_` 或全局 free page 计数。
- 返回 `nullptr` 给调用方。

替代方案是预先估算并分配所有 split 元数据。该方案也可行，但会增加额外临时数组或固定容量结构；恢复原始块更符合当前 intrusive list 风格。

### Decision: 测试优先覆盖源码不变量和可构建性

现阶段许多 allocator 行为难以在宿主单元测试中真实执行，因此测试组合采用：

- 源码级测试扫描关键 helper、顺序和旧 API 禁用。
- `xmake` 验证 freestanding cross build。
- `openspec validate --all` 验证 spec 结构。
- `uv run python tools/boot_debug.py run --no-launch` 验证 kernel/boot/image/bochsrc 资产。
- Bochs runtime smoke 仅在本机 ROM 和交互限制允许时运行；否则明确记录未运行原因。

替代方案是新增大量 debug-only kernel invariant scanner。本项目前两个 change 已明确不通过 debug-only scanner 替代测试，本 change 保持该方向。

### Decision: 本 change 不重命名 `free_pages()`

`alloc_kernel_pages()` 已经明确分配入口是 kernel virtual pages 语义，`free_pages()` 当前也在公开头文件注释和 spec 中被约束为只释放 `alloc_kernel_pages()` 返回的 kernel virtual range。本 change 保留 `free_pages()` 名称，避免把 API polish 和页表生命周期修复混在一起。

替代方案是同步改名为 `free_kernel_pages()`。该名称更对称，但会扩大 breaking 面，并分散本 change 对 PTE 清理、TLB 刷新、map 失败回滚和 buddy split 原子性的重点。只有未来做完整 kernel memory API polish 时，才重新评估是否统一释放入口命名。

### Decision: 本 change 不回收空页表页

本 change 的页表生命周期目标是：释放 mapped kernel virtual range 时清除有效 PTE、刷新当前 CPU TLB，并保证不会继续通过旧虚拟地址访问已归还的 physical backing。空 PT/PD/PDPT 页表页回收需要额外的引用计数、空表扫描或层级所有权模型，超出当前稳定化范围。

替代方案是同时实现页表页回收。该方案能减少长期页表页占用，但会显著增加回滚和并发前置设计复杂度。当前阶段只保证 unmap 后没有有效 PTE；页表页回收作为后续内存优化或页表管理 change 处理。

### Decision: direct map 作为后续独立能力设计

`KVMEM_BASE` 当前被定义为 kernel heap/vmalloc-style virtual allocation 区，不承诺 `virt = phys + offset`。direct map 会引入独立地址区、物理地址快速访问语义、页表属性和 boot 映射策略，应该作为后续独立 capability/change 设计。

替代方案是在本 change 顺带规划或实现 direct map。该方案会把 VMem 生命周期修复扩大为地址空间重构，影响 boot/linker/page-table 评审范围。因此本 change 明确不引入 direct map，只保留未来独立设计空间。

## Risks / Trade-offs

- [Risk] 清 PTE 但不回收空页表页会保留少量页表页占用 -> Mitigation: 本 change 只保证不残留有效映射，页表页回收作为后续内存优化。
- [Risk] `invlpg` helper 绑定 x86_64，未来多架构需要抽象 -> Mitigation: BigOS 当前仅支持 x86_64 boot，先放在 x86 VMem 路径内。
- [Risk] map 失败回滚逻辑复杂，可能引入新的链表或页表生命周期错误 -> Mitigation: 先写明确 helper 边界和源码级测试，再运行 cross build 与 boot asset validation。
- [Risk] buddy split 失败路径很难在正常环境触发 -> Mitigation: 通过结构化代码、源码级测试和可选 fault-injection follow-up 验证恢复逻辑，不把 debug-only scanner 放入内核生产路径。
- [Risk] 不引入 direct map 会继续让任意物理页临时访问不方便 -> Mitigation: 当前需求只覆盖 kernel heap/vmalloc-style 分配，direct map 另立 change 设计。

## Migration Plan

1. 标注并文档化 `KVMEM_BASE` / `KVMEM_LEN` 的 kernel virtual allocation 区语义。
2. 把页表映射逻辑拆成内部 map helper，并为释放路径增加 unmap/TLB flush helper。
3. 调整 `free_pages()`，确保先 unmap，再释放 physical backing 和 VMem 区间。
4. 为 map 失败路径补充回滚，避免 dangling present descriptor 和 leaked backing。
5. 加固 buddy split 元数据失败恢复逻辑。
6. 更新源码级测试和 OpenSpec spec delta。
7. 运行 `uv run pytest`、`openspec validate --all`、`xmake`、`uv run python tools/boot_debug.py run --no-launch`；可用时再运行 bounded Bochs smoke。

## Open Questions

无。`free_pages()` 命名、页表页回收和 direct map 范围均已在 Decisions 中收敛为本 change 的明确边界。
