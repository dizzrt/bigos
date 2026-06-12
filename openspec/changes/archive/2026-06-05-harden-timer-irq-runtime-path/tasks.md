## 1. 迁移 tick 状态并引入受控 timer API

- [x] 1.1 在 `kernel/core/timer/timer.cc` 定义 `volatile tick_t g_ticks`（保留 `bigos::timer::__detail`），并从 `kernel/core/irq/isr.cc` 移除该定义。
- [x] 1.2 在 `kernel/core/timer/timer.cc` 实现 IRQ-context-safe 的 `bigos::timer::on_tick() noexcept`，仅递增单调 tick，不分配/阻塞/IO/EOI。
- [x] 1.3 在 `include/bigos/timer.h` 声明 `on_tick()`，并为 `on_tick()`/`ticks()`/`mdelay()` 补充上下文调用契约注释（IRQ-only / 任意上下文只读 / 仅 IRQ-enabled 非中断上下文）。

## 2. 收敛 IRQ0 handler

- [x] 2.1 修改 `kernel/core/irq/isr.cc` 的 `implement_isr(timer)`，改为调用 `bigos::timer::on_tick()`，移除 `++bigos::timer::__detail::g_ticks`。
- [x] 2.2 保留 `BIGOS_TIMER_SMOKE` 下 bounded `serial_puts("BIGOS_TIMER_IRQ\n")` marker，确认 marker 逻辑不进入 `on_tick()`。
- [x] 2.3 确认 handler 仍不直接发送 i8259 EOI，EOI 仍由 `irq_dispatch` 在 handler 返回后统一发送。

## 3. 固化 ISR ABI 不变量（验证为主，不改 ABI）

- [x] 3.1 复核 `kernel/core/irq/interrupt.s` 的栈 16 字节对齐、通用寄存器按 `InterruptFrame` 字段顺序保存/恢复、error-code 占位槽，必要时仅补注释不改布局。
- [x] 3.2 复核 `kernel/core/irq/interrupt.cc` 的 exception 不发 EOI、external IRQ 单次 EOI 后 `iretq` 返回边界。

## 4. 更新源码级测试

- [x] 4.1 更新 `tests/test_timer_irq_foundation_source.py`：将“`on_tick` 必须不存在 / handler 直写 `g_ticks`”断言改为“`on_tick()` 存在且被 handler 调用、handler 不再直写 `g_ticks`、tick 状态定义在 timer TU”。
- [x] 4.2 新增/调整断言：`mdelay()` 与 tick 轮询不出现在任何 ISR handler body；`timer_smoke` 仍默认关闭、gated 且 bounded。
- [x] 4.3 新增覆盖 ISR ABI 不变量的源码级检查（寄存器保存顺序、error-code 占位、external IRQ 单次 EOI、exception 不发 EOI）。

## 5. 文档

- [x] 5.1 更新 `docs/en/arch/timer-irq-foundation.md`，记录硬化后的 timer/IRQ runtime 契约、`on_tick()` 所有权与三类 API 的上下文边界。

## 6. 验证

- [x] 6.1 运行最窄可用的 `xmake` / `x86_64-elf-g++` 交叉构建，确认 timer/IRQ/kernel 源码编译通过。
- [x] 6.2 运行 `uv run pytest tests/test_timer_irq_foundation_source.py` 与相关源码级检查。
- [x] 6.3 对修改的 C++ 文件检查 clang/clangd 辅助诊断（可用时）。
- [x] 6.4 Bochs/serial oracle 可用时，用 `uv run python tools/boot_debug.py` + `timer_smoke` 观测 bounded `BIGOS_TIMER_IRQ`，确认 IRQ0 周期触发、EOI、`iretq` 返回与 tick 递增。
- [x] 6.5 在 validation 记录运行时结果，或缺失依赖、已通过的 source/build 检查与剩余 IRQ runtime/ABI 风险。

## 7. OpenSpec 校验

- [x] 7.1 运行 `openspec validate harden-timer-irq-runtime-path --strict` 并修复问题。
