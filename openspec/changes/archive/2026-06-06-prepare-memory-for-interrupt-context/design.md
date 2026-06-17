## Context

BigOS 当前已经有较完整的早期内存栈：buddy physical allocator、early metadata arena、slab/kmalloc、kernel virtual memory、page-table map/unmap、`mm::self_test`。unified boot handoff capability/1.5 已启用周期性 timer IRQ0，TTY console input capability 已将 keyboard IRQ1 输入交接到 TTY fixed-capacity ring buffer。下一阶段引入 scheduler 前，allocator 必须先明确在中断上下文中的可用边界。

当前约束：

- 单核 x86_64、Legacy BIOS/i8259、kernel-owned IDT、无 scheduler、无 SMP、无用户态地址空间。
- IRQ0/IRQ1 handler 已存在，handler 设计要求不分配内存、不阻塞、不直接执行复杂输出。
- `mm_self_test()` 仍在 PIC 初始化、IRQ unmask 和 `sti` 之前运行。
- `#PF` handler 是 diagnostic-only，不恢复 fault、不分配内存、不修改页表。
- 内存地址布局、direct map、`KVMEM_BASE` heap/vmalloc 语义、BootInfo handoff ABI 和 ISR ABI 不能在本 change 中改变。

## Goals / Non-Goals

**Goals:**

- 为 allocator API 和少量只读/诊断 API 标注上下文契约，明确哪些入口禁止从 IRQ handler 调用。
- 提供最小 x86 interrupt guard，用 RAII 或等价显式 API 保存并恢复进入前 IF 状态。
- 在 allocator 内部关键元数据修改边界使用 guard，保证单核下不会被 maskable IRQ handler 交错观察到半更新状态。
- 为 timer/keyboard/#PF 等中断路径补充源码级防线，防止直接调用普通分配/释放 API。
- 更新文档和 self-test/源码测试，记录当前能力只面向单核、无 scheduler 环境。

**Non-Goals:**

- 不实现 SMP、per-CPU cache、自旋锁、抢占调度或 scheduler-aware locking。
- 不让普通 `kmalloc()`、`free()`、`alloc_kernel_pages()`、`free_pages()` 成为 IRQ handler safe。
- 不引入阻塞分配、sleep queue、wait queue、timer queue 或异步释放队列。
- 不改变 page-count vs buddy-order API 分层，不恢复旧 `alloc_pages()` 等 alias。
- 不改变 boot/linker 地址、direct map 区间、kernel heap/vmalloc 区间、IDT vector 或 `InterruptFrame` ABI。

## Decisions

### Decision: 使用“上下文契约”而不是全面 IRQ-safe allocator

普通 allocator 路径可能扩容 slab、分配页表页、修改 vmem list、回收 backing pages 或输出诊断；这些操作不适合在 IRQ handler 中执行。kernel memory API capability 先把它们标为 non-IRQ-context，并通过测试阻止 ISR 误用。

替代方案：让 `kmalloc()` 在关中断时可从 ISR 调用。该方案会迫使 slab/VMem/buddy 的失败回滚、页表映射和未来锁策略提前承诺更强并发语义，超出当前阶段。

### Decision: guard 保存并恢复 IF 状态，而不是无条件 `cli`/`sti`

critical section API 应读取当前 RFLAGS.IF，进入时执行 `cli`，退出时只在进入前 IF=1 时恢复 `sti`。这样 allocator 可以在 early boot 关中断路径、普通 IRQ-enabled 路径和嵌套 guard 中复用同一原语，不会错误打开调用方原本关闭的中断。

替代方案：每个 allocator 内部直接写裸 `cli`/`sti`。该方案容易破坏嵌套语义，也难以在测试中统一识别。

### Decision: interrupt guard 归属 IRQ 基础设施

guard 放在 `bigos::irq` 的小型 public header 中，例如 `include/irq/interrupt_guard.h` 或等价命名，并由内存代码按需 include。这样 RFLAGS/`cli`/`sti` 等 x86 interrupt 语义集中在 IRQ 层，`bigos::mm` 只消费 critical-section primitive，不拥有硬件中断控制实现。

替代方案：放在 `bigos::mm` 或顶层 `bigos` memory header。该方案会让内存层暴露 CPU interrupt 细节，或把硬件相关 helper 混入通用内核 API，不利于后续区分 interrupt guard、spinlock 和 scheduler-aware lock。

### Decision: 不公开 `interrupts_enabled()` 作为kernel memory API capability API

kernel memory API capability 暂不提供公开的 `interrupts_enabled()` 只读 helper。IF 状态读取应封装在 interrupt guard 内部；测试通过源码检查 guard 的 RFLAGS 读取、IF 保存和条件恢复逻辑，而不是鼓励普通内核代码主动分支依赖当前 IF 状态。

替代方案：公开 `interrupts_enabled()` 用于测试和诊断。该方案会过早形成可依赖 API，后续引入 scheduler、preemption 或 SMP 后容易出现调用方根据瞬时 IF 状态做错误决策。

### Decision: 只保护单核 maskable IRQ 交错，不声明 SMP 同步

本阶段 guard 只用于防止同 CPU maskable IRQ handler 在 allocator 元数据半更新期间打断当前路径。它不是自旋锁，不保护 NMI，不保护多核并发，也不提供阻塞语义。

替代方案：直接引入 lock abstraction。由于 scheduler/SMP 尚不存在，提前设计完整锁语义会让后续 change 难以区分 interrupt guard、spinlock 和 sleepable lock 的边界。

### Decision: 优先使用静态 IRQ producer buffer

timer 和 keyboard 已经证明 IRQ handler 可以通过静态状态或 fixed-capacity ring buffer 完成 handoff。本 change 明确后续 IRQ producer 若需要缓冲，应优先使用初始化前静态分配或启动期预分配结构，而不是在 handler 中动态分配。

替代方案：为 IRQ handler 提供专用 emergency allocator。当前没有真实需求，且会引入容量耗尽、回收时机和诊断复杂度。

### Decision: 验证以源码级契约和现有 runtime self-test 为主

kernel memory API capability 的主要风险是上下文误用和 guard 语义错误，适合通过源码级测试检查 public headers 注释、guard 保存恢复 IF、ISR 禁止 token、`mm_self_test()` 初始化顺序和 OpenSpec 校验。若 Bochs 可用，再补充 boot marker smoke，但不把交互式 keyboard/VGA oracle 作为本阶段必需条件。

替代方案：新增复杂 runtime race test。当前单核无 scheduler，runtime race 覆盖价值有限，源码约束更稳定。

### Decision: allocator 长路径拆分为 guard 内元数据更新与 guard 外慢操作

allocator 内部若存在页表页分配、slab 扩容、回收、批量清理或诊断输出等长路径，应在实现时拆分边界：只把 list、bitmap、accounting、backing record 和 PTE/TLB bookkeeping 等必须原子观察的元数据更新放入 guard；可能失败、耗时或触发进一步分配的慢操作保持在 guard 外，并用显式失败回滚连接两段状态。

替代方案：直接把整个 allocator entry point 包在 guard 内。该方案实现简单，但会扩大 IRQ latency，并可能把后续不适合关中断执行的路径固化进 critical section。

## Risks / Trade-offs

- [Risk] guard 覆盖太宽导致 IRQ latency 增加 -> Mitigation: 仅包围 allocator 元数据临界区，不把 `mdelay()`、console 输出或长循环放入 guard。
- [Risk] guard 覆盖太窄导致 allocator 状态仍可能被 IRQ 观察到半更新 -> Mitigation: 在 design/task 中要求逐个审视 buddy/slab/vmem list 和统计更新边界，并用源码测试固定关键 token。
- [Risk] 文档标注与实现不一致 -> Mitigation: public headers、docs 和 tests 同步更新，测试检查关键 API 的上下文标注。
- [Risk] 误把 interrupt guard 理解为 SMP lock -> Mitigation: 命名和文档明确 single-core、maskable IRQ-only，不提供跨 CPU 互斥承诺。
- [Risk] Bochs runtime smoke 仍受本机 ROM/serial oracle 限制 -> Mitigation: validation 记录可运行命令、失败原因和剩余 bootability 风险，源码级与交叉构建仍作为必需验证。

## Migration Plan

- 新增或整理 interrupt guard 头文件与实现，保持 freestanding-safe 且不依赖 hosted runtime。
- 给 public memory API 和内部 helper 增加上下文契约注释，先不改变调用方语义。
- 在 allocator 内部按最小范围套用 guard，并保持 self-test 在 IRQ enable 前运行。
- 增加源码级测试覆盖 guard、ISR 禁止 allocator、初始化顺序和文档约束。
- 若引入失败或 bootability 风险，回滚 guard 调用点即可恢复到原先单核早期行为。

## Open Questions

无。kernel memory API capability 当前已将 guard 归属、IF 状态 helper 暴露边界和 allocator 长路径处理方式收敛为上述设计决策。
