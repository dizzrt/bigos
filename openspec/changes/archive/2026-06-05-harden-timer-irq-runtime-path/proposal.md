## Why

阶段 1 的 `add-timer-irq-foundation` 已实现并归档：PIT channel 0 + i8259 IRQ0 已默认开启，IRQ0 handler 直接递增 `bigos::timer::__detail::g_ticks`，`timer::ticks()` 读取该 counter，`timer::mdelay()` 在其上 busy-wait。为快速完成 bring-up，handler 当前是一段保守的 inline 实现：tick 状态定义在 `src/kernel/irq/isr.cc` 而非 timer translation unit，handler 跨命名空间直接写 `g_ticks`，受控的 timer 内部 API（如 `timer::on_tick()`）被刻意省略，源码级测试甚至断言 `bigos::timer::on_tick();` 不存在。

在进入阶段 2（keyboard/TTY 默认启用 IRQ1）之前，需要在不引入 scheduler、抢占、SMP 或用户态的前提下，把这条保守路径打磨成更干净、可验证的 timer/IRQ runtime path，明确 IRQ-context 安全边界并补齐 ISR ABI 的 runtime 证据，降低后续阶段再次耦合未硬化 IRQ 路径的风险。

## What Changes

- 将 IRQ0 handler 直接更新 `g_ticks` 的保守实现，收敛为受控的 timer 内部 API（恢复或等价引入 `bigos::timer::on_tick()`），把 tick 状态归位到 timer translation unit；handler 只调用该 IRQ-context-safe API，不再跨命名空间裸写 `g_ticks`。
- 明确并文档化 timer API 的上下文契约：哪些 API（如 `on_tick()`）只允许在 IRQ context 调用，哪些（如 `ticks()`、`mdelay()`）只允许在非中断上下文调用，并以源码级检查锁定 `mdelay()` 不在 IRQ handler 中被调用。
- 保持 timer handler 的 freestanding/IRQ-safe 约束不变：不分配内存、不阻塞、不 `kprintf`、不依赖 scheduler/TTY/filesystem、不直接发送 i8259 EOI；EOI 仍由 external IRQ dispatch 统一在 handler 返回后发送。
- 补齐 ISR ABI 的源码级与（可用时）runtime 验证：`interrupt.s` 进入 C++ dispatch 前的 16 字节栈对齐、原始通用寄存器保存、`InterruptFrame` layout 与 error-code 槽语义、external IRQ EOI 后经 `iretq` 返回路径。
- 在稳定 runtime oracle 下复测 IRQ0 周期触发、EOI、`iretq` 返回、tick 单调递增和 `mdelay()` 行为；Bochs/serial oracle 不可用时显式记录原因与剩余 IRQ runtime 风险。
- 更新 `tests/test_timer_irq_foundation_source.py` 中与“`on_tick` 必须不存在 / handler 直写 `g_ticks`”相关的断言，使其与硬化后的内部 API 一致。
- 同步更新 `docs/arch/timer-irq-foundation.md`（或新增硬化说明），记录 timer/IRQ runtime 契约与上下文边界。

## Capabilities

### New Capabilities

- `timer-irq-runtime`: 定义硬化后的 timer/IRQ runtime path，包括受控 timer 内部 API（`on_tick`）、IRQ-context vs 非中断上下文调用契约、ISR ABI runtime 不变量（栈对齐、寄存器保存、error-code 槽、EOI 后返回）以及 runtime/源码级验证要求。

### Modified Capabilities

- `timer-irq-foundation`: 将 tick 更新从“handler 直接写 `g_ticks`、显式不提供 `on_tick`”收敛为“handler 调用受控 timer 内部 API 更新单调 tick”，并补充 tick/delay API 的上下文调用契约；不改变 PIT 初始化、注册先于 unmask、smoke 默认关闭和地址布局等既有不变量。
- `interrupt-exception-foundation`: 在不改变 exception/IRQ 分流 ABI 与 EOI 分离规则的前提下，新增 ISR 进入 C++ dispatch 的栈对齐、原始通用寄存器保存、`InterruptFrame`/error-code layout 和 external IRQ 返回路径的 runtime 级不变量要求。

## Impact

- 影响 IRQ/timer 子系统：`src/kernel/irq/isr.cc`（handler 改为调用 `on_tick()`、tick 状态迁出）、`src/kernel/timer/timer.cc` 与 `include/bigos/timer.h`（新增 `on_tick()` 及上下文契约注释）、`src/kernel/irq/interrupt.cc`（dispatch/EOI 边界复核）。
- 影响 ISR ABI 汇编：`src/kernel/irq/interrupt.s` 的栈对齐与寄存器保存约束需被源码级/runtime 验证覆盖；本 change 不改变 ABI，只补齐验证与不变量记录。
- 影响测试与工具：更新 `tests/test_timer_irq_foundation_source.py` 相关断言，新增覆盖 `on_tick` 存在、handler 经 `on_tick` 更新 tick、`mdelay` 不在 IRQ handler 调用、ISR ABI 不变量的源码级检查；Bochs 可用时复用 `tools/boot_debug.py` serial marker 做 bounded runtime smoke。
- 影响文档：更新 `docs/arch/timer-irq-foundation.md`，补充 timer/IRQ runtime 契约与上下文边界。
- 架构假设：仍为单核、x86_64 legacy BIOS + i8259 PIC + PIT 8253/8254 + Bochs 路径；不引入 scheduler、抢占、SMP、APIC/IOAPIC/HPET 或用户态。
- 内存布局假设：不移动 boot 固定地址、linker higher-half base、kernel load base、BootInfo ABI、recursive self-mapping、`KVMEM_BASE` 或 direct-map 区域；不扩展 allocator 的 IRQ-context 安全语义。
- 工具链假设：以 `xmake` + `x86_64-elf-gcc/g++` 为权威构建验证；Python 辅助验证通过 `uv run ...` 执行；Bochs/serial oracle 不可用时记录原因与剩余 bootability/IRQ runtime 风险。

## Non-Goals

- 不切换到 HPET/APIC timer、不做 TSC calibration、不实现 tickless 或高精度时钟。
- 不实现抢占调度、kernel thread、idle thread、time slice 或 scheduler sleep。
- 不引入 timer queue / timer wheel / callout / deadline timer 或阻塞 sleep 队列。
- 不引入 SMP/per-CPU tick、IPI 或跨 CPU 时间同步，不扩展 allocator 在 IRQ context 的并发契约。
- 不改变 boot/linker/memory 地址布局、BootInfo ABI、`#PF` 诊断-only 策略或 IDT/`InterruptFrame` ABI 形状。
