# 验证记录

## 已通过

- `uv run pytest tests/test_interrupt_foundation_source.py tests/test_timer_irq_foundation_source.py`
  - 结果：13 passed；新增覆盖 `isr_common` 在调用 C++ dispatch 前对齐栈、保存原始 RAX、timer IRQ0 默认开启、keyboard IRQ1 默认由 `keyboard_smoke` 控制。
- `uv run pytest`
  - 结果：40 passed。
- `xmake f --timer_smoke=n --mm_self_test=n && xmake`
  - 结果：修复 ISR 栈对齐并恢复默认 IRQ0 unmask 后，默认构建通过。
- `xmake f --timer_smoke=n --keyboard_smoke=n --mm_self_test=n && xmake`
  - 结果：默认开启 timer IRQ0、默认关闭 keyboard IRQ1 smoke 后构建通过；用户运行确认不再重启。
- `xmake f --timer_smoke=y && xmake`
  - 结果：timer smoke 构建通过。
- `xmake f --timer_smoke=y --mm_self_test=n && xmake`
  - 结果：修复 ISR 栈对齐并恢复默认 IRQ0 unmask 后，timer smoke 构建通过。
- `clang++ -std=c++17 -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/drivers/timer/pit.cc kernel/core/timer/timer.cc kernel/core/irq/isr.cc`
  - 结果：无输出，退出码 0。
- IDE diagnostics
  - 结果：`include/drivers/timer/pit.h`、`kernel/drivers/timer/pit.cc`、`include/bigos/timer.h`、`kernel/core/timer/timer.cc`、`kernel/core/irq/isr.cc`、`include/irq/interrupt.h` 无诊断。
- `openspec validate add-timer-irq-foundation --strict`
  - 结果：Change valid。

## Runtime Smoke

- 普通 boot smoke 命令：
  - `xmake f --timer_smoke=n --mm_self_test=n && uv run python tools/boot_debug.py run --serial-log build/test/normal-boot.serial.log --expect-serial-marker "BigOS kernel reached" --smoke-timeout 10`
  - 结果：Bochs 启动并构建镜像，但未在超时时间内创建/写入 serial log marker；后续发现 Bochs 子进程残留并持有 `build/test/os.raw` image lock。
- Timer smoke 命令：
  - `uv run python tools/boot_debug.py run --serial-log build/test/timer-smoke.serial.log --expect-serial-marker BIGOS_TIMER_IRQ --smoke-timeout 10`
  - 结果：同样未观测到 serial marker；`strings build/kernel` 已确认 `BIGOS_TIMER_IRQ` 与 `BigOS kernel reached` 编入 timer-smoke 构建。

## 剩余风险

- 本地 Bochs/serial oracle 行为不稳定，runtime smoke 仍需要在稳定 Bochs/ROM/serial 环境中复跑。
- 已通过源码级检查锁定 PIT 常量、IRQ0 注册先于 unmask、timer handler 不直接 EOI、memory self-test 仍早于 `initIRQ()`/`enableIRQ()`、timer smoke 默认关闭且输出有界。
- 已修复 ISR 汇编调用 C++ dispatch 前未对齐栈、原始 RAX 保存顺序错误、keyboard IRQ1 默认 unmask 干扰 timer bring-up、IRQ0 handler 跨 TU 调用 `timer::on_tick()` 等问题。
- 用户手动验证默认 timer IRQ0 开启且 keyboard IRQ1 masked 的最终形态不再无限重启；仍建议后续在稳定 Bochs/ROM/serial 环境中复跑自动 runtime smoke。
