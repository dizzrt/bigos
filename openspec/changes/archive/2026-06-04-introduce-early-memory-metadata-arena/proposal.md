## Why

当前 buddy 初始化期间仍通过 `new PageBlock` 和 intrusive list node 间接依赖静态 `kmalloc` cache 容量，这让初始化正确性依赖内存图复杂度和静态 slab 余量。需要引入 early memory metadata arena，把 buddy 元数据 bootstrap 从通用 slab/kmalloc 扩容能力中解耦出来，使 `init_mem()` 的阶段边界更清晰、更可验证。

## What Changes

- 引入仅用于内存初始化阶段的 early metadata arena，用来承载 `PageBlock`、buddy free list node 等早期元数据。
- 调整 buddy 初始化路径，使 memory map 消费阶段不依赖普通 `kmalloc` 动态扩容。
- 明确 arena 的物理来源、对齐、生命周期和切换点，确保进入正常 VMem/slab 阶段后不再从 arena 分配新对象。
- 增加容量不足检测和可诊断失败路径，避免静默破坏 free list 或页统计。
- 增加源码级验证，覆盖复杂 memory map 下 metadata 分配、统计一致性和 fallback 失败记录；boot runtime 验证复用既有 memory self-test serial marker。
- 不移动 boot 固定地址、kernel load base、higher-half base、self-mapping 地址或 `KVMEM_BASE`。
- 不引入 direct map、页表页回收、scheduler、IRQ enable、SMP 或通用 heap 替代实现。

## Capabilities

### New Capabilities
- `early-memory-metadata-arena`: 覆盖早期内存元数据 arena 的来源、使用边界、生命周期、失败处理和验证要求。

### Modified Capabilities
- `kernel-memory-correctness`: 将 buddy 初始化的不变量扩展为“不依赖普通 slab/kmalloc 动态扩容即可完成 metadata bootstrap”。

## Impact

- 影响子系统：`src/mm/buddy.*`、`src/mm/kmem.*`、`src/mm/memdef.h`，可能影响 boot handoff memory map 消费路径和内存初始化诊断输出。
- 架构假设：x86_64 freestanding kernel，BootInfo v2/v1 memory map 已规范化，低 2 MiB 和 kernel 映像仍保留不释放。
- 内存布局假设：arena 必须来自明确保留或初始化期安全使用的内存，不得覆盖 `0x0500..0x9fff` handoff/page-table 区、`0x100000` boot 页表 backing、`0x1000000` kernel load base 或 kernel 映像范围。
- API 影响：不改变公开内存 API；新增接口应优先保持在 `bigos::mm::__detail` 或内部源文件作用域。
