## Context

`init_mem()` 目前先初始化静态 slab/cache，再初始化 buddy，最后初始化 VMem。buddy 消费 BootInfo memory map 时需要创建 `PageBlock` 和 intrusive list node，这些元数据目前依赖 `new`/`kmalloc` 从静态 cache 获取。该模式在简单 memory map 下可工作，但它把 buddy bootstrap 的可靠性绑定到静态 slab 容量，内存图复杂或元数据数量增加时可能耗尽并失败。

本设计引入 early memory metadata arena，把 buddy 初始化需要的元数据从普通 slab/kmalloc 动态扩容中解耦。

## Goals / Non-Goals

**Goals:**

- 为 buddy 初始化提供明确的 early metadata arena。
- 让 memory map 消费阶段不依赖普通 `kmalloc` 动态扩容。
- 明确 arena 来源、边界、对齐、生命周期和容量不足行为。
- 保持公开内存 API、boot 固定地址和 VMem 地址布局不变。

**Non-Goals:**

- 不实现通用 bump allocator 或长期 heap 替代方案。
- 不引入 direct map、页表页回收、scheduler、IRQ enable、SMP 或锁。
- 不移动 legacy handoff、BootInfo、boot-stage page table、kernel load base 或 higher-half base。
- 不改变 BootInfo memory map ABI。

## Decisions

### Decision: arena 只服务 buddy 初始化元数据

arena 应只分配 buddy 初始化期间的 `PageBlock` 和 list node 等固定元数据，不作为 `kmalloc` 的一般后备。这样能避免早期分配器职责扩散。

替代方案是做完整 early bump allocator 并接入 `kmalloc`。该方案更通用，但会扩大 change 范围，也更容易和 slab 生命周期重构混在一起。

### Decision: arena 生命周期到 buddy 初始化完成为止

buddy 初始化阶段从 arena 分配的元数据会继续被 buddy 持有；arena 本身不再接受新分配。后续动态 split 或释放路径仍使用正常 `kmalloc/new` 分配新元数据。

替代方案是在进入正常 allocator 后迁移 arena 元数据到 slab。迁移复杂且容易破坏 free list ownership，暂不需要。

### Decision: arena 来源必须显式避开固定低地址和 kernel 映像

arena 可以来自编译期预留的 `.bss` 静态 buffer，或来自 BootInfo usable region 中明确切出且不加入 buddy 的区域。第一阶段建议优先使用静态 buffer，容量可诊断，地址风险低。

替代方案是从首个 usable region 中切分。该方案节省静态内存，但必须更早处理保留区、对齐和 memory map 修改，风险更高。

### Decision: 容量不足必须显式失败

arena 耗尽时 buddy 初始化不得继续插入半初始化节点，也不得把未完整建模的 memory region 加入 free list。失败路径应输出明确错误并 halt，或者返回 init failure 并由现有 handoff failure halt 处理。

替代方案是回退到普通 `kmalloc`。这会重新引入本 change 要消除的 bootstrap 依赖。

### Decision: boot runtime 验证复用既有 memory self-test marker

`establish-memory-runtime-validation` 已建立 `BIGOS_MM_SELF_TEST_PASSED` / `BIGOS_MM_SELF_TEST_FAILED` 串口 marker 和 `tools/boot_debug.py --memory-self-test` 路径。本 change 不新增 arena 专属 runtime oracle；arena 分配来源和耗尽行为由源码级测试覆盖，boot runtime smoke 用既有 marker 验证 arena 接入后 `init_mem()` 之后的真实 allocator 路径仍可用。

替代方案是增加 arena 专属 boot marker。该方案会扩大启动诊断协议，而且 arena 是 `init_mem()` 内部 bootstrap 机制，短期没有必要把内部状态暴露为新的 runtime oracle。

### Decision: 第一版容量使用编译期固定对象预算

第一版 arena 容量使用编译期固定对象数量，分别为初始化期 `PageBlock` 和 intrusive list node 设置预算常量。预算依据应以当前支持的最大 E820/BootInfo memory map entry 数为输入，并为每个 usable region 预留保守的初始化 split 元数据余量；实现中应通过容量统计、`static_assert` 或源码级测试把“entry 数 * split 余量”的估算关系固定下来。

不直接按运行时 memory map 动态切分 arena，也不把容量表达为“最大 split 块数”这一单一参数。前者会扩大 BootInfo usable region 修改范围，后者容易隐藏 `PageBlock` 与 list node 的不同增长模型。固定对象预算更适合作为第一版可诊断、可审查的 bootstrap 约束。

### Decision: arena 使用量只进入诊断输出

常规 `print_physical_memory_info()` 不输出 arena 使用量，避免把内部 bootstrap 实现细节加入普通启动日志。arena 耗尽路径必须输出明确错误；self-test、源码级测试或 diagnostic build 可以输出 used/capacity/high-water 等统计，用于调容量和定位复杂 memory map 问题。

替代方案是在普通 physical memory summary 中长期展示 arena 使用量。该方案有助于观察，但会把一次性初始化机制暴露为稳定日志接口，且容易让后续日志断言依赖内部容量数字。

## Risks / Trade-offs

- 静态 arena 容量估计不足 -> 提供容量统计和耗尽诊断，后续可根据 memory map 复杂度调整。
- 静态 arena 增加 kernel `.bss` 占用 -> 控制对象类型和容量，避免把通用 early allocations 放入 arena。
- arena 对象释放语义特殊 -> 明确初始化期 PageBlock 元数据由 buddy 持有，不单独释放 arena backing。
- 复杂 memory map 仍可能超出容量 -> 记录失败原因并保留后续从 usable region 切分 arena 的升级方向。

## Migration Plan

1. 增加 early metadata arena 类型和只分配固定元数据的接口。
2. 修改 buddy 初始化 memory map 消费路径，使 `PageBlock` 和 node 从 arena 创建。
3. 保持运行期 split 路径继续使用正常 allocator，并明确初始化期/运行期分配来源。
4. 增加源码级测试覆盖 arena 不依赖普通 kmalloc 扩容，并复用既有 memory runtime self-test 验证接入后 boot 路径。
