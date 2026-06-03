## Context

`harden-memory-correctness` 修复了内存管理的若干不变量，并初步引入 `alloc_kernel_pages(nr_pages, flags)` 与 `alloc_physical_order(order, flags)`。但当前代码仍保留旧别名和遗留声明：

- `include/bigos/memory.h` 仍公开 `alloc_pages(uint32_t __pages, gfm_t __gfm)`。
- `src/mm/buddy.h` 仍声明 `alloc_physical_pages(uint32_t __order, gfm_t __gfm)` 与 `free_physical_pages()`。
- `src/mm/kmem.h` 仍声明未实现或未使用的 `kmem_cache_alloc()`、`kmem_memory_alloc_pages()`。
- `_GFM_PRE_PAGING` 仍暴露为普通 `gfm_t` flag，容易让调用方误以为 `kmalloc()` 是否可访问取决于外部传参。

本 change 不改变当前内存管理分层：

```text
kmalloc/new
  -> CacheChain / Cache / Slab
     -> alloc_kernel_pages(nr_pages, flags)
        -> VMem virtual range
        -> alloc_physical_order(order, flags) for mapped backing
```

它只把 API 边界和命名语义固定下来，避免后续驱动、KTL 容器或 kernel 子系统继续依赖模糊入口。

## Goals / Non-Goals

**Goals:**

- 公开头文件只暴露语义明确的 kernel virtual pages 接口：`alloc_kernel_pages(nr_pages, flags)` 与 `free_pages(ptr)`。
- 物理页分配接口只在 `mm::__detail` 内部按 buddy order 命名：`alloc_physical_order(order, flags)` 与 `free_physical_order(ptr)`。
- 移除旧 alias 和遗留声明，确保源码搜索不会再出现新的 `alloc_pages()` 或 `alloc_physical_pages()` 调用点。
- 明确 `kmalloc()` 和全局 `operator new` 返回地址必须已经可直接访问。
- 将 `_GFM_PRE_PAGING` 约束为内存管理内部或低层页映射策略，不作为普通调用方的必备知识。
- 为 API 清理增加构建、源码扫描、clang/clangd 辅助检查和可用时的 boot smoke test。

**Non-Goals:**

- 不引入 scheduler、IRQ enable、SMP、锁或 per-CPU allocator。
- 不设计完整 direct map，不把 `KVMEM_BASE` 改成 direct-map 区。
- 不引入 demand paging、copy-on-write、用户态地址空间、页权限模型或 TLB shootdown。
- 不移动 boot-stage PML4、self-mapping 地址、低地址 handoff 区、kernel load base 或 higher-half base。
- 不解决 `harden-memory-correctness` 中遗留的页表失败回滚和 buddy split 元数据失败问题；这些应作为后续 correctness follow-up。
- 不引入 early allocator，也不通过扩大静态 slab 容量掩盖 bootstrap 元数据风险。

## Decisions

### Decision: 移除旧 alias，而不是继续兼容

本 change 应删除 `alloc_pages()` 和 `alloc_physical_pages()` 的声明与定义，并迁移仓库内所有调用点。旧名称太短，无法表达它到底返回虚拟地址、物理地址、页数还是 order；继续保留会让第二阶段 API 澄清失去约束力。

替代方案是把旧 alias 标记为 deprecated。当前 freestanding kernel 没有完整 warning 策略，且项目仍处于早期 bring-up，直接删除更能尽早暴露错误调用点。

### Decision: 保留 `free_pages()`，但文档化为 kernel virtual pages 释放

`free_pages(ptr)` 目前释放的是 VMem 管理的 kernel virtual range，并在有 backing 时释放 physical pages。虽然名称不如 `free_kernel_pages()` 明确，但调用点较少，且 proposal 重点是消除分配入口的页数/order 歧义。本 change 保留它，同时在头文件注释、spec 和测试中明确它只配对 `alloc_kernel_pages()`。

替代方案是同步重命名为 `free_kernel_pages()`。这会进一步清晰，但扩大 breaking 面；本 change 不做该重命名，后续也不单独为了名称对称启动 breaking change，只有在更完整的 kernel memory API polish 中需要统一释放入口时再评估。

### Decision: `kmalloc/new` 不暴露 `_GFM_PRE_PAGING`

普通 `kmalloc(size)` 和 `operator new(size)` 调用方必须获得当前页表下可访问的内核地址。slab grow 内部可以继续使用 `_GFM_PRE_PAGING` 强制预映射，但外部调用方不应为了让对象可访问而传这个 flag。

替代方案是要求所有可能触发 slab grow 的调用方显式传 `_GFM_PRE_PAGING`。这会把 allocator 内部映射细节泄漏给 KTL 容器、驱动和 kernel 子系统，且容易产生隐蔽 page fault。

### Decision: `_GFM_PRE_PAGING` 暂时保留，但降低可见语义

本 change 不删除 `_GFM_PRE_PAGING`，因为 `alloc_kernel_pages()` 仍需要区分“只保留虚拟区间”和“立即映射 backing”的低层行为。但任务应检查它没有被普通调用点滥用，并在注释中说明它是内存管理内部/低层 flag。

替代方案是拆出 `reserve_kernel_pages()` 与 `alloc_mapped_kernel_pages()` 两个 API。这个方向更清晰，但属于下一阶段 `stabilize-kernel-vmem-layout` 或更完整 API 重构；本 change 只先收紧 `_GFM_PRE_PAGING` 的可见语义。

### Decision: 不改变地址布局和页表公式

本 change 只清理 API，不改变 `KVMEM_BASE`、`KVMEM_LEN`、`KERNEL_PML4_ADDR`、self-mapping 地址公式、linker higher-half base 或低地址保留策略。

替代方案是顺带把 `KVMEM_BASE` 重命名为 `KERNEL_VMALLOC_BASE` 或引入 direct-map 规划。该工作涉及地址空间布局设计，应留给后续 change。

## Risks / Trade-offs

- [Risk] 删除 alias 会导致隐藏调用点编译失败 -> Mitigation: 先用源码搜索迁移所有 `alloc_pages()`、`alloc_physical_pages()` 调用点，再运行 `xmake` 和 clang/clangd 辅助检查。
- [Risk] 外部用户如果已依赖旧公开头文件会被 breaking change 影响 -> Mitigation: 当前项目是早期 kernel，仓库内调用点优先；proposal 明确标注 **BREAKING**。
- [Risk] 保留 `free_pages()` 仍存在命名不完全对称 -> Mitigation: 在头文件注释和 spec 中明确它释放 kernel virtual pages；不为名称对称单独启动后续 breaking rename。
- [Risk] `_GFM_PRE_PAGING` 继续存在可能被新代码滥用 -> Mitigation: 增加源码扫描任务，限制普通调用点只通过 allocator 内部路径使用它。
- [Risk] 本 change 不修复页表失败回滚与 buddy split 失败路径 -> Mitigation: 在任务中显式记录为后续 correctness follow-up，不把它混入 API 澄清范围。

## Migration Plan

1. 删除 `include/bigos/memory.h` 中 `alloc_pages()` 声明，保留并注释 `alloc_kernel_pages()` 与 `free_pages()` 的配对语义。
2. 删除 `src/mm/vmem.cc` 中 `alloc_pages()` wrapper，确认仓库内无调用点。
3. 删除 `src/mm/buddy.h` 与 `src/mm/buddy.cc` 中 `alloc_physical_pages()`、`free_physical_pages()` wrapper，迁移所有调用点到 `alloc_physical_order()`、`free_physical_order()`。
4. 清理 `src/mm/kmem.h` 中未实现或未使用的 allocator 遗留声明。
5. 检查 `_GFM_PRE_PAGING` 使用点，确保普通 `kmalloc/new` 调用方不需要传入该 flag。
6. 增加或更新源码级测试，扫描旧 alias、遗留声明和误用调用点。
7. 运行 `xmake`、OpenSpec 校验、clang/clangd 辅助诊断；Bochs 可用时执行 boot smoke test。

## Follow-up Decisions

- 后续不单独为了分配/释放名称对称把 `free_pages()` 重命名为 `free_kernel_pages()`；只有在更完整的 kernel memory API polish 中需要统一释放入口时，才把该重命名作为同一批 breaking change 评估。
- 后续 VMem 布局或 API 重构应优先引入 `reserve_kernel_pages()` 与 `alloc_mapped_kernel_pages()`，用显式 API 表达“仅保留虚拟区间”和“立即建立 backing mapping”，并逐步替代普通调用方可见的 `_GFM_PRE_PAGING` flag 语义。
