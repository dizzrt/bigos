## 1. 契约梳理

- [x] 1.1 复核 `include`、`kernel/mm`、`kernel/core/irq` 和现有内存文档，列出 public allocator、internal buddy/vmem helper、只读统计和 IRQ handler 的当前调用边界。
- [x] 1.2 确认 `mm_self_test()` 仍在 `init_mem()` 之后、PIC 初始化/IRQ unmask/`sti` 之前运行，并记录阶段 3 不改变该初始化顺序。
- [x] 1.3 明确本 change 不修改 boot/linker 地址、direct map、`KVMEM_BASE` heap/vmalloc 语义、BootInfo handoff ABI、IDT vector 或 `InterruptFrame` ABI。

## 2. Interrupt Guard

- [x] 2.1 新增最小 single-core interrupt guard 或等价 critical-section primitive，进入时保存当前 IF 状态并执行 `cli`。
- [x] 2.2 实现 guard 退出路径：仅在进入前 IF 为 enabled 时恢复 `sti`，进入前 IF disabled 时保持 disabled。
- [x] 2.3 保持 guard freestanding-safe，不依赖 hosted libc、异常、RTTI、线程、scheduler 或动态分配。
- [x] 2.4 在 public/internal header 注释中说明 guard 只覆盖 same-CPU maskable IRQ interleaving，不提供 SMP/NMI/阻塞/调度锁语义。

## 3. 内存 API 标注

- [x] 3.1 为 `kmalloc()`、`free()`、`alloc_kernel_pages()`、`free_pages()` 和全局 `new/delete` 相关声明补充 non-IRQ-handler-safe 契约说明。
- [x] 3.2 为只读统计或诊断 helper 标注 context-agnostic、IRQ-disabled-only 或 non-interrupt-context-only 边界。
- [x] 3.3 复核并保持 page-count vs buddy-order API 分层，确认不新增 `alloc_pages()`、`alloc_physical_pages()` 或 `free_physical_pages()` 等混合语义 alias。

## 4. Allocator Critical Sections

- [x] 4.1 审视 buddy 分配、split、free、merge、`PageBlock` 生命周期和 free-page accounting，给可被 IRQ-enabled 路径交错观察的元数据更新添加最小 guard。
- [x] 4.2 审视 slab/kmalloc cache list、slab bitmap、large allocation record、allocation-kind metadata、reclaim 和 accounting，给关键元数据更新添加最小 guard。
- [x] 4.3 审视 VMem free/used range list、physical backing record、PTE 写入/清除和 TLB invalidation bookkeeping，给关键元数据更新添加最小 guard。
- [x] 4.4 确认 guarded region 不包含 `mdelay()`、长时间 busy-wait、bulk console/serial output、filesystem、scheduler、user-mode 或其它阻塞式路径。
- [x] 4.5 复核失败回滚路径，确保 guard 不掩盖 allocation failure、partial mapping rollback、slab reclaim 或 buddy split metadata failure 的既有行为。

## 5. IRQ 路径约束

- [x] 5.1 检查 timer IRQ0 handler，确认不调用 `kmalloc()`、`free()`、`alloc_kernel_pages()`、`free_pages()`、global `new/delete` 或 `mdelay()`。
- [x] 5.2 检查 keyboard IRQ1 handler，确认仍只执行 bounded scancode decode 和 fixed-capacity TTY enqueue，不做动态分配、复杂输出或阻塞等待。
- [x] 5.3 检查 diagnostic-only `#PF` handler，确认不分配/释放内存、不修改页表恢复、不重试 faulting instruction。
- [x] 5.4 若新增 IRQ producer handoff，使用静态或 boot-time-prepared bounded storage，并记录 overflow/drop 策略。

## 6. 文档更新

- [x] 6.1 更新内存或中断架构文档，记录 allocator 上下文契约、interrupt guard 语义、当前 non-goals 和后续 scheduler/SMP 前置关系。
- [x] 6.2 更新路线图或 validation notes，说明阶段 3 完成后仍不承诺 IRQ handler 动态分配、SMP safety 或阻塞分配。
- [x] 6.3 在文档中记录 Bochs/runtime smoke 可用性边界，区分源码级验证、交叉构建验证和 emulator bootability 风险。

## 7. 测试与验证

- [x] 7.1 新增或扩展源码级测试，检查 allocator API 上下文标注、interrupt guard 保存/恢复 IF token、ISR 禁止普通 allocator token 和 `mm_self_test()` 初始化顺序。
- [x] 7.2 运行相关 Python 测试，例如 `uv run pytest tests/<memory_interrupt_context_test>.py tests/test_interrupt_foundation_source.py tests/test_timer_irq_foundation_source.py`；若测试文件名不同，在 validation 中记录实际命令。
- [x] 7.3 运行默认 `xmake` 或最窄可用交叉构建，确认 C++/assembly 改动可由目标工具链构建。
- [x] 7.4 运行 freestanding C++17 辅助静态检查（例如 `x86_64-elf-g++ -fsyntax-only` 或等价 clang/clangd 检查）覆盖新增/修改的 C++ 源和头；若 clang/clangd 不适配 freestanding 配置，记录原因和剩余风险。
- [x] 7.5 若 Bochs 和 serial oracle 可用，运行 bounded boot smoke；若不可用，记录缺失依赖、替代检查和剩余 bootability 风险。
- [x] 7.6 运行 `openspec validate prepare-memory-for-interrupt-context --strict` 并修复问题。
- [x] 7.7 新增 `validation.md`，分开记录已通过检查、未运行检查及原因、历史诊断和当前 change 引入/修复的问题。
