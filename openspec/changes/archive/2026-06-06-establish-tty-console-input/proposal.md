## Why

阶段 1 和阶段 1.5 已把 PIT/i8259 IRQ0 与 timer/IRQ runtime path 收敛到可维护的基础上，下一步需要把当前仅用于 smoke 的 keyboard IRQ1 升级为最小可用输入路径，并为后续 scheduler、shell、系统调用和驱动调试提供统一 console/TTY 抽象。

当前主线只有 VGA/COM1 早期输出和 keyboard scancode smoke；`tmp/terminal-keyboard-wip/` 中的历史原型未接入活动源码，且存在 ISR 内直接输出、`terminal`/`pty` 命名漂移和默认启用 IRQ1 等问题，需要通过新的 OpenSpec change 明确安全边界后再合入。

## What Changes

- 新增最小 keyboard 输入管线：PS/2 set-1 scancode 读取、修饰键状态、US 键盘布局下的 key/ASCII 转换，以及面向 TTY 的输入入队接口。
- 新增固定容量输入环形缓冲，要求 keyboard ISR 只读取 scancode、做有限解码并入队，不在 IRQ context 中分配内存、阻塞、调用 `kprintf`/复杂 formatter 或直接写 VGA。
- 新增最小 TTY/console 抽象，统一命名到 `bigos::terminal` 或等价明确命名空间，提供初始化、字符输入消费、字符/字符串输出与基础控制字符处理。
- 将普通 console 输出封装到 VGA text 后端之上，减少后续 runtime 路径零散直写；COM1 serial 保留给 bounded smoke marker 和 early diagnostics，不默认 mirror 普通 console 字符流。
- 调整 `kernel()` 初始化顺序，在 IDT/PIC/keyboard handler 注册完成、TTY 输入缓冲可用后，才允许从 smoke 或默认路径 unmask keyboard IRQ1。
- 保留 `keyboard_smoke` 作为验证开关，并补充源码级检查、构建检查和可用时的 Bochs keyboard smoke 记录。

## Capabilities

### New Capabilities

- `tty-console-input`: 定义 BigOS 最小 keyboard 输入、TTY 输入缓冲、console 输出抽象、IRQ-context 安全边界、初始化顺序和验证要求。

### Modified Capabilities

- `interrupt-exception-foundation`: 扩展 keyboard IRQ1 的使用要求，从仅 smoke handler 约束为注册先于 unmask、handler 不直接 EOI、不在 IRQ context 中执行复杂输出/阻塞/分配，并继续由 external IRQ dispatch 统一发送 EOI。

## Impact

- 影响输入与 IRQ 子系统：`src/kernel/irq/isr.cc`、`include/irq/isr.h`、`include/irq/interrupt.h`，需要把 keyboard handler 从 smoke-only 逐步连接到输入队列，同时保留现有 i8259 EOI 分离规则。
- 影响输出与 console 子系统：`src/kernel/bigos/io.cc`、`include/bigos/io.h`、`src/drivers/video/vga.cc`、`include/drivers/video/vga.h`，需要抽象 console 后端并明确哪些早期输出 API 保持为诊断直写路径。
- 影响 kernel 初始化：`src/kernel/kernel.cc` 需要启用或新增 `init_tty()`/console 初始化，并明确其相对内存、IRQ、`sti` 和 keyboard IRQ1 unmask 的顺序。
- 影响构建与验证：`xmake.lua` 的 `keyboard_smoke` 开关继续默认关闭；新增/更新源码级测试覆盖 scancode/keymap、ring buffer、ISR 安全边界、IRQ1 unmask 顺序和 console API。
- 架构假设：仍为单核 x86_64、legacy BIOS + i8259 PIC + PS/2 keyboard + VGA text mode + COM1 serial；不引入 APIC/IOAPIC、SMP、scheduler、进程或用户态。
- 内存布局假设：不移动 boot 固定地址、linker higher-half base、kernel load base、BootInfo ABI、recursive self-mapping、`KVMEM_BASE`、direct-map 区域或 allocator API 语义。
- 工具链与 emulator 假设：以 `xmake` + `x86_64-elf-gcc/g++` 为构建验证；Python 辅助验证通过 `uv run ...`；Bochs/serial/keyboard oracle 不可用时记录原因和剩余输入 runtime 风险。
- 非目标：不实现多 TTY、完整 ANSI/VT 终端、终端转义序列、作业控制、行编辑历史、阻塞 sleep、shell、系统调用、用户态程序、完整键盘 layout 抽象或热插拔设备模型。
