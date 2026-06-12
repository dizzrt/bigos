# 验证记录

## 已通过

- `xmake`（默认配置，`--timer_smoke=n --mm_self_test=n`）
  - 结果：timer/IRQ/kernel 源码编译通过；`kernel/core/timer/timer.cc`、`kernel/core/irq/isr.cc`、`kernel/core/irq/interrupt.cc`、`kernel/core/irq/interrupt.s` 均重新编译成功。
- `xmake f --timer_smoke=y --mm_self_test=n && xmake`
  - 结果：timer smoke 构建通过；`strings build/kernel | grep -c BIGOS_TIMER_IRQ` 为 `1`，确认 bounded marker 已编入 smoke 构建。
- `uv run pytest tests/test_timer_irq_foundation_source.py`
  - 结果：9 passed。新增/更新覆盖：`on_tick()` 存在且被 timer handler 调用、handler 不再直写 `g_ticks`、tick 状态定义在 timer TU、`mdelay()`/tick 轮询不在任何 ISR handler body、三类 timer API 上下文契约注释、ISR ABI 不变量（寄存器保存顺序、error-code 占位、16 字节栈对齐、external IRQ 单次 EOI、exception 不发 EOI）。
- `uv run pytest`
  - 结果：46 passed。
- `clang++ -std=c++17 -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only kernel/core/timer/timer.cc kernel/core/irq/isr.cc kernel/core/irq/interrupt.cc`
  - 结果：无输出，退出码 0。
- IDE diagnostics
  - 结果：`kernel/core/irq/isr.cc` 等修改文件无诊断。
- `openspec validate harden-timer-irq-runtime-path --strict`
  - 结果：Change valid。

## Runtime Smoke

- Timer smoke 命令：
  - `xmake f --timer_smoke=y --mm_self_test=n && xmake`
  - `uv run python tools/boot_debug.py run --serial-log build/test/timer_smoke.log --expect-serial-marker BIGOS_TIMER_IRQ --smoke-timeout 60`
  - 结果：未在超时时间内观测到 `BIGOS_TIMER_IRQ` serial marker，serial log 未生成。
  - 原因：`tools/boot_debug.py run` 在 `build_kernel()` 中强制执行 `xmake f --mm_self_test=n` 重新配置工程，该步骤会把 `timer_smoke` 复位回默认值 `false`，因此实际 boot 的内核未编入 `BIGOS_TIMER_SMOKE` bounded marker；当前工具未暴露 `--timer-smoke` 开关。
  - 已确认的间接证据：在 `--timer_smoke=y` 构建下 `strings build/kernel` 含 `BIGOS_TIMER_IRQ`，证明 marker 逻辑正确编入 smoke 构建。

## 剩余风险

- Runtime IRQ0 周期触发、EOI、`iretq` 返回与 tick 单调递增的端到端 serial 观测仍未完成：`boot_debug.py` 的固定重配置会清掉 `timer_smoke`，需要在工具支持 timer-smoke 开关、或手工生成已开启 `BIGOS_TIMER_SMOKE` 的镜像并接入稳定 Bochs/ROM/serial oracle 后复跑。该 runtime 不确定性与阶段 1（`add-timer-irq-foundation`）记录的本地 Bochs/serial 不稳定一致。
- 跨 TU 调用 `bigos::timer::on_tick()` 在高频 IRQ0 下的实际行为仅由源码级检查与构建覆盖，尚未经稳定 oracle runtime 复核。
- 已通过源码级检查锁定：tick 状态归位 timer TU、handler 经 `on_tick()` 更新且不裸写 `g_ticks`、handler 不直接 EOI、`mdelay()`/tick 轮询不在 ISR body、timer smoke 默认关闭且 bounded、ISR ABI 寄存器保存顺序/error-code 占位/栈对齐/EOI 边界不变。
- 本 change 未改变 `InterruptFrame` 布局、寄存器保存顺序、EOI 分离规则或任何地址布局/ABI 形状，仅做封装收敛与验证固化。
