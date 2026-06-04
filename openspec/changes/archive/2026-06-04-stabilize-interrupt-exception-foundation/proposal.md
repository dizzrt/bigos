## Why

BigOS 目前已有 IDT stub、默认 ISR、i8259 PIC 驱动和 `initIRQ()` 接入点，但中断仍未真正启用，键盘 IRQ 没有完整接线，异常与外部 IRQ 的处理边界也不清晰。完成内存 runtime validation、early metadata arena 和 slab lifecycle 后，内核已经具备更稳定的早期内存基线，适合先把中断/异常基础设施推进到可安全启用和可诊断状态，再继续 direct map 等内存路线。

## What Changes

- 稳定 x86_64 IDT 初始化路径，明确 kernel 阶段 IDT descriptor 的存放、加载和 gate 属性。
- 修正或重构 ISR assembly ABI，使无 error-code 异常、有 error-code 异常和外部 IRQ 都向 C++ handler 传递稳定的 interrupt frame。
- 区分 CPU exception 和 external IRQ，避免异常路径错误发送 PIC EOI。
- 为 `#PF` 提供诊断型 handler，读取 `CR2`、解析 page fault error code、输出稳定 marker/日志并安全 halt。
- 完成 i8259 PIC 初始化接入，明确 remap、mask/unmask、EOI 的调用顺序。
- 接入 keyboard IRQ1 的最小 handler，读取 PS/2 data port scancode，输出可观测 marker 或字符，不引入完整 TTY。
- 将 `kernel()` 初始化顺序收敛为：VGA -> memory -> optional memory self-test -> IDT/exception -> PIC/keyboard -> enable IRQ -> boot reached loop。
- 增加源码级检查、构建验证和 Bochs smoke 验收记录，确认普通 boot、keyboard IRQ 和诊断型 `#PF` 行为可观测。
- 不实现 scheduler、线程、用户态、syscall、完整 TTY、APIC/IOAPIC、SMP、TLB shootdown、demand paging、copy-on-write 或用户地址空间 page fault recovery。

## Capabilities

### New Capabilities

- `interrupt-exception-foundation`: 定义 BigOS 早期 x86_64 中断与异常基础能力，包括 IDT/ISR ABI、CPU exception 诊断、i8259 PIC 外部 IRQ、keyboard IRQ smoke 和验证要求。

### Modified Capabilities

- `kernel-memory-runtime-validation`: 明确 memory self-test 仍运行在 IRQ/PIC 启用之前，并且本 change 不改变其单核、关中断验证边界。

## Impact

- 影响 boot/kernel 子系统：`src/kernel/kernel.cc` 的初始化顺序和 IRQ enable 时机。
- 影响 IRQ 子系统：`src/kernel/irq/interrupt.s`、`src/kernel/irq/interrupt.cc`、`src/kernel/irq/isr.cc`、`include/irq/interrupt.h`、`include/irq/isr.h`。
- 影响 driver 子系统：`src/drivers/irqchip/i8259.cc`、`include/drivers/irqchip/i8259.h`，以及新增或接入 keyboard IRQ 相关代码。
- 可能新增测试或源码级检查：`tests/` 下针对 IDT/ISR/PIC/keyboard/#PF 行为的静态验证，必要时扩展 boot debug smoke marker。
- 架构假设：仅覆盖 x86_64 legacy BIOS + i8259 PIC + Bochs 路径；不引入 APIC/IOAPIC 或 UEFI 专属中断模型。
- 内存布局假设：不移动 linker higher-half base、kernel load base、BootInfo handoff ABI、page table self-mapping 地址或 roadmap 中尚未定义的 direct map 区域。
- 工具链假设：以 `xmake` + `x86_64-elf-gcc/x86_64-elf-g++` 为权威构建验证；Bochs 可用时执行有界 runtime smoke，不可用时必须记录原因和剩余 bootability 风险。
