# Validation

## 已通过

- `uv run pytest tests/test_tty_console_input_source.py tests/test_interrupt_foundation_source.py tests/test_timer_irq_foundation_source.py`：23 passed。
- `uv run pytest`：52 passed。
- `xmake`：默认配置构建通过。
- `xmake f --keyboard_smoke=y && xmake && xmake f --keyboard_smoke=n`：keyboard smoke 显式配置构建通过，并已恢复 `keyboard_smoke` 关闭。
- `x86_64-elf-g++ -std=c++17 -ffreestanding -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=kernel -fno-rtti -fno-exceptions -Iinclude -Icpp/include -Icpp/libsupc++/include -fsyntax-only src/kernel/terminal/keyboard.cc src/kernel/terminal/tty.cc src/kernel/terminal/console.cc src/kernel/irq/isr.cc src/kernel/kernel.cc src/drivers/video/vga.cc`：通过。
- IDE diagnostics：当前修改文件未报告诊断。
- `openspec validate establish-tty-console-input --strict`：通过。

## Runtime Smoke

- `uv run python tools/boot_debug.py run --serial-log build/test/tty-console.serial.log --expect-serial-marker "BigOS kernel reached" --smoke-timeout 30`：kernel 和 boot image 构建通过，但 Bochs serial smoke 在 30 秒内未观测到 `BigOS kernel reached` marker。
- 当前 API 环境无法提供交互式 VGA/manual keyboard oracle；本阶段按设计不扩展 `tools/boot_debug.py` 自动注入 scancode，因此未声明 keyboard IRQ1 到 TTY 的人工 runtime smoke 通过。

## 剩余风险

- 源码级检查和交叉构建覆盖了 scancode decode、TTY ring buffer、console API、handler-before-unmask、dispatch-owned EOI 和 ISR safety 的关键不变量。
- 实际 PS/2 keyboard delivery、VGA 可见 echo/consumer 行为和人工键盘输入仍需在可稳定观测 VGA/serial 且可交互输入的 Bochs 环境中复核。
