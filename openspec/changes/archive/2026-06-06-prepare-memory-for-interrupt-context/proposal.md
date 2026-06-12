## Why

阶段 2 已把 keyboard IRQ1 输入交接接入主线，阶段 4 将引入调度器和内核线程；在进入调度前，需要先把 allocator 在 IRQ disabled、IRQ handler 和普通内核上下文中的可用边界写清楚并用源码检查固定下来。

当前内存子系统已经具备 buddy、slab/kmalloc、kernel virtual memory 和 runtime self-test，但仍默认只承诺单核早期路径。阶段 3 先建立最小 interrupt-context 契约，避免后续 timer/keyboard/driver/scheduler 代码在 IRQ handler 中误用可能阻塞、扩容或修改页表的 allocator API。

## What Changes

- 为内存 API 增加明确的上下文安全标注：区分 IRQ handler 可调用、IRQ disabled 可调用、普通非中断上下文调用和禁止中断上下文调用的入口。
- 引入最小 interrupt guard / critical-section 抽象，用于单核下保存并恢复 IF 状态，为未来调度器锁和 SMP 自旋锁预留一致入口。
- 收敛 allocator 内部关键元数据更新边界，在不引入 SMP、抢占调度或阻塞语义的前提下，确保现有 self-test 和源码检查能覆盖上下文契约。
- 更新内存、IRQ 或架构文档，记录当前 allocator 不承诺从 IRQ handler 动态分配普通对象或虚拟页的限制，以及允许的只读统计/诊断边界。
- 补充源码级测试，防止 timer/keyboard IRQ handler、`#PF` 诊断路径或未来 ISR 直接调用 `kmalloc()`、`alloc_kernel_pages()`、`free_pages()` 等非 IRQ-safe API。

非目标：

- 不实现 SMP、per-CPU allocator cache、自旋锁或抢占调度。
- 不实现阻塞分配、wait queue、sleep queue 或 timer queue。
- 不改变 buddy order/page-count 语义、kernel heap/direct-map 地址布局、BootInfo handoff ABI、IDT/ISR ABI 或现有 `#PF` diagnostic-only 行为。
- 不把普通 `kmalloc()`/`alloc_kernel_pages()` 宣称为 IRQ handler safe；若需要 IRQ producer 缓冲，优先使用静态固定容量结构。

## Capabilities

### New Capabilities

- `memory-interrupt-context`: 定义 BigOS 早期内存管理在单核、i8259 IRQ、无 scheduler/SMP 环境下的上下文安全契约、critical-section 行为和验证要求。

### Modified Capabilities

无。本 change 通过新增 capability 补充上下文边界，不改变既有 `kernel-memory-correctness` 对 buddy/slab/vmem 分配正确性的要求。

## Impact

- 受影响子系统：`kernel/mm`、`include/bigos/memory.h` 或相关内存 public headers、`kernel/core/irq`、`kernel/core/kernel.cc` 初始化顺序、`docs/en/arch` 内存/中断文档、`tests` 源码级验证。
- 架构假设：x86_64、单核、Legacy BIOS/i8259、kernel-owned IDT、无 scheduler、无 SMP、无用户态地址空间。
- 内存布局假设：保持 higher-half kernel、kernel heap/vmalloc 区、direct map 区和页表 self-mapping 现有布局不变。
- 工具链/模拟器假设：继续使用 xmake、`x86_64-elf-g++`、`uv run pytest`、OpenSpec 校验；Bochs runtime smoke 如不可稳定运行，需在 validation 中记录原因和剩余风险。
