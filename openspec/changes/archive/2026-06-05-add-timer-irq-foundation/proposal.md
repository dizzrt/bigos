## Why

BigOS 已完成早期诊断统一和 kernel direct map，对应能力边界 的基线巩固已经收尾。当前中断/异常基础设施支持 kernel-owned static IDT、稳定 `InterruptFrame` dispatch ABI、i8259 PIC remap、CPU exception 与 external IRQ 分流、keyboard IRQ1 smoke，以及统一的 EOI 发送边界；但内核仍没有周期性时间源。

后续调度器、超时、驱动延时和更稳定的 boot smoke 都需要一个最小、可观测、可验证的 timer IRQ0 基础。`add-timer-irq-foundation` 先在 legacy i8259 + PIT 8253/8254 路径上建立周期性 tick，不引入 scheduler、抢占、APIC/HPET 或完整时间子系统。

## What Changes

- 新增 PIT/8253/8254 channel 0 驱动，使用显式端口常量和固定目标频率配置周期性 IRQ0。
- 为 i8259 remap 后的 timer vector `0x20` 注册 early timer handler，并在 handler 注册完成后才 unmask IRQ0。
- 提供 freestanding-safe 的单调 tick counter 和最小 busy-wait delay API，例如 `ticks()`、`mdelay()` 或等价命名。
- 新增默认关闭的 timer smoke 构建开关，输出稳定 `BIGOS_TIMER_IRQ` marker，供 Bochs/serial smoke 观测。
- 保持 CPU exception 与 external IRQ 分流、C++ external IRQ dispatch 统一发送 EOI 的既有边界；timer handler 自身不直接发送 PIC EOI。
- 增加源码级检查、交叉构建、clang/clangd 辅助诊断和可选 Bochs smoke 验证记录。
- 不实现 scheduler、线程、抢占、APIC/IOAPIC、HPET、tickless、动态 timer queue、阻塞 sleep 或用户态时间 API。

## Capabilities

### New Capabilities

- `timer-irq-foundation`: 定义 BigOS 早期 PIT timer IRQ0 能力，包括 PIT 初始化、IRQ0 注册/unmask 顺序、tick counter、busy-wait delay、timer smoke marker 和验证要求。

### Modified Capabilities

- `interrupt-exception-foundation`: 复用既有 i8259 external IRQ dispatch 与 EOI 规则；新增 timer IRQ0 的注册/unmask 不变量，但不改变 exception/IRQ 分流 ABI。
- `kernel-memory-runtime-validation`: 明确 memory self-test 仍必须在 IRQ/PIC/timer 初始化和 `enableIRQ()` 之前运行。

## Impact

- 影响 boot/kernel 子系统：`kernel/core/kernel.cc` 或 IRQ 初始化聚合点需要在启用 IRQ 前完成 timer 初始化。
- 影响 IRQ 子系统：`include/irq/interrupt.h`、`kernel/core/irq/interrupt.cc`、`kernel/core/irq/isr.cc` 或等价注册路径需要加入 timer vector/handler。
- 影响 driver 子系统：新增或接入 PIT driver，例如 `kernel/drivers/timer/pit.cc` 与对应 public/internal header。
- 影响构建配置：新增默认关闭的 validation 开关，例如 `timer_smoke` -> `BIGOS_TIMER_SMOKE`。
- 影响测试与工具：新增源码级测试覆盖 timer 注册先于 unmask、memory self-test 仍在 IRQ 前、timer handler 不直接 EOI、不依赖 scheduler/heap；Bochs 可用时用 serial marker 做 bounded smoke。
- 架构假设：仅覆盖 x86_64 legacy BIOS + i8259 PIC + PIT 8253/8254 + Bochs 路径；不引入 APIC/IOAPIC/HPET/UEFI 专属 timer 模型。
- 内存布局假设：不移动 boot 固定地址、linker higher-half base、kernel load base、BootInfo ABI、recursive self-mapping、`KVMEM_BASE` 或 direct-map 区域。
- 工具链假设：以 `xmake` + `x86_64-elf-gcc/x86_64-elf-g++` 为权威构建验证；Python 辅助验证通过 `uv run ...` 执行；Bochs 不可用时必须记录原因和剩余 bootability/IRQ 风险。
