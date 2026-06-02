## Why

当前 `src/mm` 已经具备 buddy、slab/kmalloc 和内核虚拟内存雏形，但多个早期正确性问题会影响启动后的动态分配、页表映射和 allocator 元数据一致性。现在先收敛为单核、关中断、无 scheduler 的早期内核内存管理加固，避免后续 IRQ、驱动或 C++ 容器继续建立在不稳定的分配语义上。

## What Changes

- 修复物理 buddy 分配器的 split/merge、释放链表和统计不变量问题，避免错误合并、use-after-free 和空闲页计数漂移。
- 修复 slab/kmalloc 的静态 size class 定义、动态扩容失败处理和对象释放路径，使 `kmalloc/new` 的小对象分配行为可预测。
- 修复内核虚拟内存分配器的 first-fit、空闲页计数、释放合并和页表索引问题，确保虚拟页分配不会复用或破坏错误区间。
- **BREAKING**: 拆分当前混淆页数与 buddy order 的页分配 API，明确 kernel virtual pages 与 physical order allocation 的边界。
- 明确本 change 不引入 scheduler/IRQ/SMP 并发支持，不重排 boot/linker 固定地址，不新增 debug-only allocator 不变量检查函数。
- 增加面向 allocator 不变量的测试验证路径；若本地缺少 cross toolchain 或 Bochs 配置，必须明确记录无法完成的运行时验证。

## Capabilities

### New Capabilities
- `kernel-memory-correctness`: 约束早期内核内存管理在物理页、虚拟页、slab/kmalloc 和页表映射上的基本正确性不变量。

### Modified Capabilities

## Impact

- 受影响子系统：`src/mm` 内存管理模块，包括 `buddy.cc/.h`、`slab.cc/.h`、`kmem.cc/.h`、`vmem.cc/.h`、`memdef.h` 和公开入口 `include/bigos/memory.h`。
- 受影响调用方：全局 `operator new/delete`、KTL 容器、未来驱动或内核组件通过 `kmalloc/free` 与 `alloc_pages/free_pages` 使用内存。
- API 影响：本 change 会拆分页分配接口，调用方需要迁移到按页数分配 kernel virtual pages 的 API 或按 order 分配 physical pages 的内部 API。
- 架构假设：x86_64 long mode、4 KiB 页、现有 boot-stage PML4 位于物理 `0x2000`，现有 self-mapping 地址公式在本 change 中只修正确性，不重新设计布局。
- 内存布局假设：保留低 2 MiB、kernel 物理加载基址 `0x1000000`、higher-half kernel base `0xffffffff80000000`、当前 `KVMEM_BASE` 所代表的内核虚拟分配区。
- 工具链假设：使用现有 xmake 与 `x86_64-elf-*` 交叉工具链构建，Bochs 仅作为可用时的 smoke test。
- 非目标：不引入 scheduler、IRQ enable、SMP、用户态、页换出、完整 direct map、NUMA、per-CPU allocator、early allocator、大幅静态 slab 扩容、debug-only 不变量检查函数或通用虚拟内存权限模型。
