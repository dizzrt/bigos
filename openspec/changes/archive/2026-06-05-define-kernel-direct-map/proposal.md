## Why

当前 `KVMEM_BASE` 已被明确为 kernel heap/vmalloc-style 虚拟分配区，不承诺
`virt = phys + offset`。随着后续 MMIO 驱动、页表页回收、任意物理页检查和块设备方向推进，
内核需要一个独立、命名清晰、可验证的 direct-map 区域来访问已知安全的物理内存，而不是
重新解释现有 heap/vmalloc 地址空间。

## What Changes

- 新增 kernel direct map 能力：定义独立于 `KVMEM_BASE` 的 direct-map 虚拟地址 base、
  长度、权限和页表映射策略。
- 新增 `bigos::mm` 下的最小地址转换 API，例如 `phys_to_direct()` /
  `direct_to_phys()` 或等价命名，明确只对 direct-map 覆盖范围内的普通物理内存有效。
- 在内存初始化过程中建立 direct-map 页表映射，覆盖 BootInfo 中已识别的 ordinary RAM 范围，
  跳过 ACPI/firmware reserved、MMIO/framebuffer 等非普通 RAM 区域，并保持 higher-half kernel、
  low identity map、recursive self-mapping 和
  `KVMEM_BASE` 布局不变。
- 为 direct-map 访问增加最小 runtime/source 级验证，确认 `KVMEM_BASE` 仍不是 direct map，
  direct-map 地址转换可逆，并能通过 direct map 访问受控 buddy-allocated RAM 页。
- 更新内存布局文档或注释，明确 direct map 与 kernel heap/vmalloc、物理 buddy、页表页回收
  和未来 MMIO 映射之间的边界。

## Capabilities

### New Capabilities
- `kernel-direct-map`: 定义内核 direct-map 区域的地址布局、映射范围、转换 API、安全边界、
  与既有 `KVMEM_BASE` heap/vmalloc 区的分区约束，以及可复现验证要求。

### Modified Capabilities
<!-- 现有 `kernel-memory-correctness` 已要求 `KVMEM_BASE` 不是 direct map；本 change 新增
     独立 direct-map capability，不改变现有 allocator requirement，故此处留空。 -->

## Impact

- 受影响子系统：内存初始化与页表映射（`kernel/mm`）、公共内存头文件（`include/mm` 或
  `include/bigos`）、内核内存布局文档/注释、内存 runtime self-test。
- 受影响地址布局：新增 direct-map 虚拟区间；不移动 `0xffffffff80000000` higher-half
  kernel base、boot 固定地址、`KERNEL_PML4_ADDR`、recursive self-mapping window、
  low identity map 或 `KVMEM_BASE`。
- API：新增 direct-map 地址转换 helper；不恢复 `alloc_pages()`、`alloc_physical_pages()`、
  `free_physical_pages()` 等旧 alias，不改变 `alloc_kernel_pages(nr_pages, flags)` 与
  `alloc_physical_order(order, flags)` 语义。
- 验证：xmake 交叉构建、OpenSpec 校验、source-level 内存布局测试、可选
  `BIGOS_MM_SELF_TEST` boot smoke；Bochs 或 cross toolchain 不可用时记录缺口和剩余
  bootability 风险。

## 假设与非目标

假设：
- 架构仍为 x86_64 四级页表，单核、早期关中断、无 scheduler、无 SMP、无用户态地址空间。
- BootInfo memory map 已经过现有 boot handoff 规范化；direct map 首版只覆盖 ordinary RAM，
  不把 ACPI/firmware reserved、MMIO/framebuffer 自动纳入普通 direct-map RAM 语义。
- Bochs、`x86_64-elf-gcc`/`x86_64-elf-g++` 与 xmake 是主要验证环境；Python 辅助工具通过
  `uv run ...` 执行。

非目标：
- 不实现用户态地址空间、syscall、进程、demand paging、copy-on-write 或 page fault 恢复。
- 不引入 DMA API、IOMMU、缓存属性管理、PAT/MTRR 编程或完整 MMIO mapping allocator。
- 不实现空页表页回收；本 change 只为后续回收提供稳定页表访问基础。
- 不启用 scheduler、IRQ 并发分配语义、SMP TLB shootdown 或 per-CPU allocator。
