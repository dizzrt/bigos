## Context

当前 kernel virtual memory 已经稳定为两层语义：

- `KVMEM_BASE = 0xffff880000000000`，长度 `0x10000000000`，用于
  `alloc_kernel_pages()`、slab grow 和未来 vmalloc-style 分配。
- recursive self-mapping window 位于 `0xffff800000000000` 相关地址区间，用于通过当前
  `KERNEL_PML4_ADDR = 0x2000` 操作四级页表。

历史 `stabilize-kernel-vmem-layout` 已明确：`KVMEM_BASE` 不是 direct map，不能承诺
`virt = phys + offset`。但后续页表页回收、MMIO/驱动 bring-up、物理页诊断和块设备方向都
需要一种稳定方式访问“已知安全的物理地址”。因此本 change 引入独立 kernel direct-map
能力，避免把 heap/vmalloc 分配区重新解释为物理地址线性映射。

## Goals / Non-Goals

**Goals:**

- 定义独立 direct-map 虚拟窗口，默认建议 `KDIRECT_BASE = 0xffff900000000000`，
  `KDIRECT_LEN = 0x400000000000`（64 TiB），与 recursive self-mapping、`KVMEM_BASE`、
  higher-half kernel 映像和 boot identity mapping 不重叠。
- 建立 direct-map 页表映射，使覆盖范围内的安全物理内存满足
  `direct = KDIRECT_BASE + phys`，并提供可逆转换 helper。
- 明确 direct map 只表达 RAM 物理地址快速访问语义，不自动表达 MMIO、DMA buffer、
  cache attribute、device memory 或用户态映射语义。
- 在 `init_mem()` / `init_vmem()` 附近保持初始化顺序清晰：buddy/slab/VMem 继续按既有
  分层初始化，direct map 的页表页分配失败必须回滚或触发统一 panic，不能留下半映射
  present PTE。
- 为后续 `reclaim-empty-page-tables` 提供稳定页表访问基础，但不在本 change 回收页表页。

**Non-Goals:**

- 不移动 `0xffffffff80000000` higher-half kernel base、boot fixed addresses、
  `KERNEL_PML4_ADDR`、recursive self-mapping window、low identity map 或 `KVMEM_BASE`。
- 不实现 user page table、syscall、process、demand paging、copy-on-write 或 `#PF` 恢复。
- 不实现 MMIO mapping allocator、PAT/MTRR/cache attribute 管理、DMA API、IOMMU 或
  TLB shootdown。
- 不改变 `alloc_kernel_pages(nr_pages, flags)` 与 `alloc_physical_order(order, flags)` 的
  语义，不恢复旧 allocator alias。

## Decisions

### Decision: direct map 使用独立高半区窗口

采用 `0xffff900000000000` 作为默认 direct-map base，长度先固定为 64 TiB。该窗口位于
canonical higher-half，避开当前 self-mapping `0xffff8000...`、`KVMEM_BASE`
`0xffff8800...` 和 kernel image `0xffffffff80000000` 所在高端区域。

替代方案是复用 `KVMEM_BASE`。该方案已被现有 memory correctness spec 排除，因为
`KVMEM_BASE` 是 first-fit virtual allocation 区，返回地址与 physical backing 没有固定线性
关系。

替代方案是选择 Linux 风格 `0xffff888000000000`。该地址过于接近现有 `KVMEM_BASE`，容易让
后续读者误以为两者同属一个窗口，因此不采用。

### Decision: API 只暴露范围受限的地址转换

新增 helper 应位于 `bigos::mm`，建议命名为：

- `constexpr uintptr_t KDIRECT_BASE`
- `constexpr uintptr_t KDIRECT_LEN`
- `bool is_direct_mapped_phys(uint64_t phys, uint64_t len = 1) noexcept`
- `void *phys_to_direct(uint64_t phys) noexcept`
- `uint64_t direct_to_phys(const void *addr) noexcept`

`phys_to_direct()` 只对 `[0, KDIRECT_LEN)` 且已由 direct map 覆盖的 ordinary RAM 地址有效。
首版实现对越界、未覆盖或非 RAM 地址返回显式失败值（`nullptr` 或 `false`，取决于最终函数
签名），不在普通转换 helper 内触发 `kpanic`。`direct_to_phys()` 对不属于 direct-map window
的虚拟地址同样返回显式失败值（例如 `false` 或约定的 invalid physical address），避免把
调用方可处理的探测错误升级为致命停机。

如果后续需要“内核不变量必须成立”的 checked helper，可以在独立 API 中封装
`phys_to_direct_or_panic()` 之类语义；首版不混用可恢复转换与致命断言。

替代方案是提供裸宏 `KDIRECT_BASE + phys`。该方案无法表达边界检查和未来覆盖范围差异，容易
被 MMIO/保留区误用。

### Decision: 首版只覆盖 ordinary RAM，不覆盖 ACPI/firmware reserved 区域

direct map 的初始覆盖范围应来自 BootInfo memory map 中内核已识别为 ordinary RAM 的物理
地址范围，包括 buddy 可分配 RAM，以及内核/boot 已消费但物理上属于 ordinary RAM 的区间
（例如 kernel image、boot handoff 数据、页表页）。ACPI reclaim/NVS、firmware reserved、
device/MMIO、framebuffer 或其它非 ordinary RAM-like 区域不在首版覆盖范围内。

这样可以让 direct map 成为“RAM 快速访问”能力，而不是早期固件表解析或 MMIO 映射能力。
如果未来需要读取 ACPI table 或 firmware reserved 数据，应在对应 capability 中显式扩展覆盖
策略或增加专门 mapping API。

数据流：

```text
BootInfo memory map
  -> normalize/classify ordinary RAM ranges
  -> clamp to KDIRECT_LEN and page-align
  -> create PML4/PDPT/PD/PT descriptors via recursive self-mapping
  -> mark direct-map leaves present | writable | non-user
  -> self-test validates reversible translation on a controlled buddy page
```

实现可以先使用 4 KiB PTE，保持与现有 VMem map/unmap helper 一致；后续若需要性能优化，可在
独立 change 中引入 2 MiB large page。

### Decision: 页表页分配失败走统一诊断路径

direct map 是内存初始化基础设施，不适合以“部分可用”的形式继续启动。若创建 direct-map
descriptor 或 backing metadata 失败，实现必须清理本次已写入的 descriptor/PTE，或在尚无
可安全回滚路径时通过 `bigos::kpanic` 输出稳定错误码并停机。

替代方案是返回 `false` 并让 `kernel()` 继续启动。该方案会让后续页表访问、driver 和 smoke
测试面对不确定 direct-map 覆盖范围，风险高于早期显式失败。

### Decision: 不把 MMIO 纳入本 change

MMIO 需要设备内存属性、缓存策略和访问 ordering 约束；direct map 当前只覆盖普通 RAM。未来
驱动可以基于 direct map 访问 RAM 数据结构，但 device BAR、APIC、framebuffer 等应由独立
MMIO mapping API 处理。

### Decision: runtime smoke 只验证受控 buddy page

首版 runtime smoke 只验证通过 buddy/allocator 获得的已知安全 RAM page：写入可控 pattern、
通过 direct-map 地址读取/写回并验证转换可逆，最后释放该 page 并保持 allocator 统计恢复。

不在首版 runtime smoke 中额外读取 kernel image 或 BootInfo 所在物理页。那些地址虽然通常应
位于 ordinary RAM，但访问语义会绑定 boot loader 布局、只读/占用策略和 handoff 生命周期，
会让 direct-map smoke 夹带额外 boot ABI 假设。kernel image/BootInfo direct-map 可见性可由
source-level 布局检查或后续 boot ABI 专项验证覆盖。

## Risks / Trade-offs

- [Risk] direct-map window 与未来地址布局冲突 -> Mitigation: 在 spec 和 source-level 测试中
  固定与 `KVMEM_BASE`、self-mapping、higher-half kernel 的不重叠关系。
- [Risk] 映射过大导致页表页消耗增加 -> Mitigation: 只映射 BootInfo 中 ordinary RAM range，
  而不是无条件铺满 64 TiB；先用 4 KiB PTE 保守实现，后续再评估 large page。
- [Risk] 将 MMIO 错误地当作 RAM direct map 使用 -> Mitigation: helper 命名和 spec 明确只覆盖
  ordinary RAM，MMIO mapping 是非目标。
- [Risk] 初始化失败后留下半映射页表 -> Mitigation: 初始化路径实现回滚或统一 panic；
  validation 覆盖失败行为和不残留无效 present descriptor 的源码级检查。
- [Risk] clang/clangd 对 freestanding x86_64 交叉环境误报 -> Mitigation: 任务中要求记录辅助
  静态检查与 GCC cross-build 的差异，不用 clang 替代 xmake 交叉构建。

## Migration Plan

1. 添加 direct-map 常量和 helper 声明，保持现有 allocator API 不变。
2. 在页表操作代码中抽出可复用 map helper，或为 direct map 添加专用初始化路径。
3. 在内存初始化早期建立 direct-map 映射，并在失败时回滚或统一 panic。
4. 扩展 `BIGOS_MM_SELF_TEST` 或 source-level 测试，验证地址布局、受控 buddy page 的转换
   可逆性和 `KVMEM_BASE` 非 direct-map 语义。
5. 更新路线图/内存布局文档，说明 direct map 与后续页表页回收、MMIO 的边界。

Rollback 策略：如果 direct-map 初始化影响 bootability，可保持 helper 声明和文档，临时关闭
runtime 初始化调用或通过 build switch 隔离验证路径；不得把 `KVMEM_BASE` 作为回退 direct map。
