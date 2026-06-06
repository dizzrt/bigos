## Context

`add-timer-irq-foundation`（阶段 1）已实现并归档，当前 timer/IRQ 运行时形态如下：

```text
i8259 IRQ0 fires
  -> interrupt.s stub: push regs, build InterruptFrame, align stack, call irq_dispatch
  -> irq_dispatch(frame)            # src/kernel/irq/interrupt.cc
       -> is_i8259_external_irq?
       -> spurious check
       -> handler = isr_list[vector] (= isr_timer)
       -> isr_timer(frame)          # src/kernel/irq/isr.cc
            ++bigos::timer::__detail::g_ticks;   # 直接裸写
            [BIGOS_TIMER_SMOKE] bounded serial_puts("BIGOS_TIMER_IRQ")
       -> i8259::send_eoi(irq_line) # dispatch 统一发送 EOI
  -> iretq
```

关键现状与约束：

- tick 状态 `volatile tick_t g_ticks` 定义在 [isr.cc](`src/kernel/irq/isr.cc`)（`timer::__detail` 命名空间），但物理上落在 IRQ translation unit 而非 timer translation unit。
- IRQ0 handler 直接 `++bigos::timer::__detail::g_ticks`，没有受控的 `timer::on_tick()` 封装；[timer.cc](`src/kernel/timer/timer.cc`) 只暴露 `ticks()` 和 `mdelay()`。
- 现有源码级测试 [test_timer_irq_foundation_source.py](`tests/test_timer_irq_foundation_source.py`) 显式断言 `bigos::timer::on_tick();` 不存在、handler 直写 `g_ticks`，因此硬化必须同步更新这些断言。
- EOI 已由 [irq_dispatch](`src/kernel/irq/interrupt.cc`) 在 handler 返回后统一发送，handler 不直接 EOI。
- `mdelay()` 在 [timer.cc](`src/kernel/timer/timer.cc`) 中 busy-wait 轮询 `ticks()`，依赖 IRQ0 持续推进 tick——在 IRQ disabled 或 IRQ context 中调用会死循环。
- ISR ABI（栈对齐、寄存器保存、`InterruptFrame` layout、error-code 槽）由 [interrupt.s](`src/kernel/irq/interrupt.s`) 与 [interrupt.h](`include/irq/interrupt.h`) 共同约定，目前缺少针对 runtime 行为的明确不变量记录与验证。

约束：单核、早期可关中断、无 scheduler/SMP/用户态；不得移动任何 boot/linker/memory 地址或改变 IDT/`InterruptFrame` ABI 形状。

## Goals / Non-Goals

**Goals:**

- 引入受控的 IRQ-context-safe timer 内部 API `bigos::timer::on_tick()`，让 IRQ0 handler 通过它更新单调 tick，而非跨命名空间裸写 `g_ticks`。
- 把 tick 状态归位到 timer translation unit，使 IRQ 层只通过 timer API 交互。
- 明确并文档化 timer API 的上下文契约：`on_tick()` 仅 IRQ context；`ticks()` 任意上下文只读；`mdelay()` 仅在 IRQ 已启用的非中断上下文调用。
- 补齐 ISR ABI 的源码级与（可用时）runtime 不变量：16 字节栈对齐、原始通用寄存器保存、`InterruptFrame`/error-code layout、external IRQ EOI 后 `iretq` 返回。
- 在稳定 oracle 下复测 IRQ0 周期触发、tick 单调递增、`mdelay()` 行为，并更新源码级测试。

**Non-Goals:**

- 不切换 HPET/APIC/TSC，不做 tickless/高精度时钟。
- 不实现 scheduler、抢占、kernel thread、idle thread、scheduler sleep。
- 不引入 timer queue/wheel/callout/deadline timer 或阻塞 sleep。
- 不引入 SMP/per-CPU tick，不扩展 allocator IRQ-context 并发契约。
- 不改变 boot/linker/memory 地址布局、BootInfo ABI、`#PF` 诊断-only 策略、IDT/`InterruptFrame` ABI 形状或 EOI 分离规则。

## Decisions

### Decision: 恢复受控 timer 内部 API `on_tick()` 并迁移 tick 状态

`g_ticks` 的定义与递增逻辑 SHALL 归位到 timer translation unit（`src/kernel/timer/timer.cc`，状态保留在 `bigos::timer::__detail`）。新增 IRQ-context-safe 的 `bigos::timer::on_tick() noexcept`，只做最小工作：原子地（在单核关中断 IRQ context 下即简单递增）推进单调 tick counter。IRQ0 handler 改为调用 `bigos::timer::on_tick()`，不再出现 `++bigos::timer::__detail::g_ticks`。

理由：阶段 1 为规避“在高频 IRQ0 handler 中跨 translation unit 调用尚未充分 runtime 验证的 timer 内部函数”而采用裸写；阶段 1.5 的目标正是在稳定 oracle 下验证该跨 TU 调用，从而恢复封装、收敛所有权、为阶段 4 scheduler tick hook 预留干净落点。

替代方案：保持裸写并仅补文档——被否决，因为它把 timer 内部状态语义永久泄漏到 IRQ 层，违背所有权清晰原则，且 scheduler 阶段会再次硬编码。

`on_tick()` 必须保持 inline/简单，不分配、不阻塞、不 `kprintf`、不发送 EOI，符合现有 handler freestanding 约束。smoke marker 逻辑（`BIGOS_TIMER_SMOKE` 下 bounded `serial_puts`）保留在 IRQ handler 内，不进入 `on_tick()`，以免把 validation-only IO 混入 timer 内部 API。

### Decision: 显式 timer API 上下文契约

通过头文件注释与文档明确：

- `on_tick()`：仅允许在 IRQ context（timer ISR）调用；推进 tick，不得阻塞或 IO。
- `ticks()`：任意上下文可读，只返回当前单核 early-kernel 下的单调快照，不保证 SMP 一致性。
- `mdelay()`：仅允许在 IRQ 已 `sti` 的非中断上下文调用；它 busy-wait 依赖 IRQ0 推进 tick，在 IRQ disabled 或 IRQ handler 中调用会死等。

源码级检查 SHALL 锁定 `mdelay()`/`ticks()` 轮询不出现在任何 ISR handler body 中，且 `on_tick()` 不出现在非 IRQ 上下文。

### Decision: 固化 ISR ABI runtime 不变量（验证为主，不改 ABI）

本 change 不改变 `interrupt.s` 的寄存器保存顺序或 `InterruptFrame` 布局，而是把既有约定写成可检查的不变量：

- 进入 `irq_dispatch` 前栈按 System V AMD64 要求 16 字节对齐。
- 通用寄存器按 `InterruptFrame` 字段顺序完整保存/恢复。
- CPU exception 不发送 PIC EOI；external IRQ（含 vector `0x20`）仅在 handler 返回后发送一次 EOI，再 `iretq` 返回。
- error-code 槽对无 error-code 向量填充占位，保持 frame 布局稳定。

理由：阶段 2 默认启用 keyboard IRQ1，会更频繁走 ISR ABI 路径；提前把不变量固化为源码级/runtime 检查可避免回归。

### Decision: runtime 验证以 Bochs serial marker 为 oracle

复用 `tools/boot_debug.py` 与 `timer_smoke` 开关，在 Bochs 下观测 bounded `BIGOS_TIMER_IRQ` marker，确认 IRQ0 周期触发、EOI、`iretq` 返回与 tick 推进闭环。Bochs/ROM/serial oracle 不可用时，记录缺失依赖、已通过的 source/build 检查与剩余 IRQ runtime 风险。

### Decision: `on_tick()` 不预留 scheduler tick hook 占位接口

本 change 的 `bigos::timer::on_tick()` SHALL 保持最小封装，仅推进单调 tick counter，不引入任何 scheduler tick hook、callback 注册点或预留参数/返回值。scheduler tick hook 留待阶段 4（`introduce-kernel-threads-scheduler`）在有真实调度需求时再引入。

理由：阶段 1.5 的目标是验证与封装收敛，而非提前设计调度耦合。当前没有 scheduler，预留占位接口属于过度设计，且会在没有使用者的情况下增加 IRQ-context API 表面与维护成本；阶段 4 引入 scheduler 时再扩展 `on_tick()` 语义即可，届时能基于真实约束定义 hook 契约。

替代方案：在本 change 预留一个空 hook 或可选 callback——被否决，因为它违背“最小封装、不为假设需求设计”的原则，且会让 IRQ-context 安全契约提前承载未验证的调度语义。

## Risks / Trade-offs

- [Risk] 跨 TU 调用 `on_tick()` 在高频 IRQ0 下若被去优化或引入意外副作用，可能影响 IRQ 延迟 → Mitigation: 保持 `on_tick()` 极简（仅递增 volatile 计数），在 Bochs smoke 下确认 marker 与 tick 推进正常。
- [Risk] 更新源码级测试断言可能与归档 change 的历史断言冲突 → Mitigation: 明确这是阶段 1.5 对阶段 1 的演进，更新断言为“`on_tick` 必须存在且被 handler 调用、handler 不再裸写 `g_ticks`”，并在 tasks/validation 记录变更理由。
- [Risk] `mdelay()` 上下文契约仅靠注释/文本检查，运行期仍可能被误用 → Mitigation: 文档与源码级 grep 检查双重约束，并在契约中显式说明 IRQ-disabled 死等风险。
- [Risk] Bochs/serial oracle 本地不稳定或不可用 → Mitigation: 显式记录原因、已通过的 build/static 检查与剩余 runtime 风险，不谎报 runtime 验证。
- [Trade-off] 本 change 只做验证与封装收敛，不引入 tick callback 抽象，避免过度设计；scheduler tick hook 留待阶段 4。

## Migration Plan

1. 在 `src/kernel/timer/timer.cc` 定义 `g_ticks` 与 `on_tick()`，从 `isr.cc` 移除 `g_ticks` 定义。
2. 在 `include/bigos/timer.h` 声明 `on_tick()` 并补充三个 API 的上下文契约注释。
3. 修改 IRQ0 handler 调用 `bigos::timer::on_tick()`，保留 `BIGOS_TIMER_SMOKE` bounded marker。
4. 复核 `interrupt.cc`/`interrupt.s` 的 EOI 与寄存器保存边界，补充必要注释（不改 ABI）。
5. 更新 `tests/test_timer_irq_foundation_source.py`：断言 `on_tick` 存在并被 handler 调用、handler 不再直写 `g_ticks`、`mdelay`/`ticks` 不在 ISR body、smoke 仍 gated+bounded。
6. 更新 `docs/en/arch/timer-irq-foundation.md`，记录 runtime 契约与上下文边界。
7. 运行交叉构建、`uv run pytest` 源码级检查、clang/clangd 辅助诊断，可用时运行 Bochs serial smoke。
8. 在 validation 记录 runtime 结果或缺失依赖与剩余风险。

回滚：本 change 局限于 timer/IRQ 封装与验证，回滚即恢复 handler 直写 `g_ticks` 并还原测试断言，不涉及地址布局或 ABI 变更。

## Open Questions

- 暂无。`on_tick()` 是否预留 scheduler tick hook 占位接口已收敛为决策：不预留，保持最小封装，待阶段 4（`introduce-kernel-threads-scheduler`）再引入（见 Decisions 节）。
