## Why

当前内核早期失败路径分散且不一致：buddy、slab、kmem、self-test、IRQ/异常分发各自
直接写 `kprintf`/`serial_puts` 后裸 `while(true){hlt}`，且 marker 风格不统一（例如
`"invalid boot memory map"`、`"slab debug guard: ..."` 没有 `BIGOS_` 前缀，无法被
serial-marker smoke 稳定识别）。`disableIRQ()` 仅在 IRQ 子系统的 `halt_cpu()` 里
执行，mm 的 halt 路径没有先关中断。随着后续阶段（timer、TTY、调度、用户态）引入更多
失败点，这种散落模式会持续放大，且缺少统一的错误码与诊断快照，给 emulator smoke 判定
和未来调试带来负担。本 change 在引入更多子系统之前，先把早期致命错误收敛到一个统一的
freestanding-safe panic/诊断底座。

## What Changes

- 新增统一的早期内核诊断/panic 设施（位于 `bigos` 内核 API 命名空间），提供：
  - 稳定的 `kpanic(code, msg, ...)` 风格入口，固定输出 `BIGOS_PANIC` marker、
    稳定错误码（枚举）、来源标识和可选上下文，然后关中断并安全 `hlt`。
  - 统一的致命诊断输出同时写 COM1 串口与 VGA，复用既有 `serial_puts`/`kprintf`。
- 将现有分散的致命失败路径迁移到统一设施，**不改变**它们当前“安全停机”的行为语义：
  - `src/mm/buddy.cc`：`halt_memory_handoff_failed()`、`halt_early_metadata_exhausted()`。
  - `src/mm/slab.cc` 与 `src/mm/kmem.cc`：`BIGOS_SLAB_DEBUG` guard 的 halt 分支。
  - `src/mm/self_test.cc`：`fail(stage)` 失败路径（保留既有
    `BIGOS_MM_SELF_TEST_FAILED stage=` 兼容输出）。
  - `src/kernel/irq/interrupt.cc`：异常/`#PF` 的 `halt_cpu()` 收敛为统一关中断 +
    panic 停机原语（保留既有 `BIGOS_EXCEPTION` / `BIGOS_PAGE_FAULT` 诊断输出）。
- 统一致命错误 marker 前缀，使所有“致命停机”路径都带 `BIGOS_` 前缀，可被
  `boot_debug.py --expect-serial-marker` 识别。
- 提供可选的诊断快照聚合入口：在 panic 时可附带 allocator 统计（复用现有
  `print_slab_stats()` / early arena 用量），按需输出，不强制。
- **非破坏性**：不改变 allocator 的非致命返回失败语义（`kmalloc` 失败仍返回
  `nullptr`，分配回滚路径不变），不改变现有 marker 字符串的“成功”侧含义。

## Capabilities

### New Capabilities
- `early-kernel-diagnostics`: 定义早期内核统一致命诊断与 panic 行为，包括稳定错误
  码、固定 `BIGOS_PANIC` marker、串口+VGA 双输出、关中断后安全停机，以及 freestanding
  安全约束和可选诊断快照。

### Modified Capabilities
<!-- 现有内存与中断相关 spec 的“可观察行为契约”不变（仍是安全停机 + 既有诊断 marker），
     本 change 只是把实现收敛到统一设施，不修改这些 spec 的 requirement，故此处留空。 -->

## Impact

- 受影响子系统：内核入口/运行时诊断（`src/kernel`）、内存管理失败路径（`src/mm`：
  buddy/slab/kmem/self_test）、中断与异常分发停机路径（`src/kernel/irq`）。
- 受影响代码：新增统一诊断头文件（如 `include/bigos/panic.h`）与实现（如
  `src/kernel/bigos/panic.cc`）；改写上述文件中的裸 `while(true){hlt}` 与零散
  `kprintf` 致命输出。
- API：新增 `bigos::kpanic(...)` 及错误码枚举；不移除现有公共内存/中断 API。
- 输出契约：新增 `BIGOS_PANIC` marker；保留 `BIGOS_EXCEPTION`、`BIGOS_PAGE_FAULT`、
  `BIGOS_MM_SELF_TEST_FAILED`、`BIGOS_MM_SELF_TEST_PASSED` 等既有 marker。
- 验证：xmake 交叉构建；可选 clang/clangd 辅助静态检查；Bochs panic smoke（如
  `page_fault_smoke` 触发后观察停机 marker），或在 emulator 不可用时记录缺口。

## 假设与非目标

假设：
- 单核、早期关中断、无 scheduler/SMP/用户态地址空间；x86_64 + higher-half 内核布局。
- 输出后端为现有 VGA 文本模式与 COM1 串口；Bochs 为本地模拟器。
- 不改动 boot 固定地址、linker higher-half base、kernel load base、self-mapping
  地址、BootInfo handoff ABI、IDT 布局或 `InterruptFrame` ABI。

非目标：
- 不实现完整 kernel logging subsystem、日志分级或环形日志缓冲。
- 不实现 symbolication、stack unwinding 或持久化 crash dump。
- 不引入可恢复异常处理（`#PF` 仍为诊断-only，不做恢复/demand paging）。
- 不改变 allocator 的非致命失败返回语义，不引入新的并发/锁模型。
