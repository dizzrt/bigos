## 1. 现状审查与边界确认

- [x] 1.1 复查 `kernel/core/kernel.cc`、`kernel/core/irq/interrupt.cc`、`kernel/core/irq/isr.cc`、`include/irq/interrupt.h`，确认 `initIRQ()`、external IRQ dispatch、EOI 和 `enableIRQ()` 的当前顺序。
- [x] 1.2 复查 `kernel/drivers/irqchip/i8259.cc` 与 `include/drivers/irqchip/i8259.h`，确认 IRQ0/IRQ1 mask、unmask、EOI 语义。
- [x] 1.3 确认当前没有 PIT/timer driver、timer smoke 开关或 tick API，避免复用 `tmp/` 归档原型而未审查。
- [x] 1.4 明确本 change 不改变 boot 固定地址、linker higher-half base、kernel load base、BootInfo ABI、recursive self-mapping、`KVMEM_BASE` 或 direct-map 布局。

## 2. PIT Driver 与 Timer API

- [x] 2.1 新增 PIT/8253/8254 driver header/source，定义 channel 0 data port `0x40`、command port `0x43`、PIT base frequency 和目标 `TIMER_HZ`。
- [x] 2.2 实现 PIT channel 0 初始化，计算 divisor 并写入 command/data ports，保持 freestanding-safe 和显式硬件访问顺序。
- [x] 2.3 定义 timer tick counter 与只读 tick API，例如 `ticks()` 或等价命名，明确单核 early kernel 语义。
- [x] 2.4 实现最小 busy-wait `mdelay()`/`sleep()` 或等价 primitive，文档说明不让出 CPU、不阻塞、不依赖 scheduler，且精度只满足 early smoke/driver delay。
- [x] 2.5 确认 timer API 不分配内存、不访问 filesystem、不依赖 TTY、scheduler、用户态或 hosted runtime。

## 3. IRQ0 接入与初始化顺序

- [x] 3.1 在 IRQ 常量中定义 timer vector 或确认 `IRQ_LINE_TIMER = 0` 到 vector `0x20` 的映射清晰可读。
- [x] 3.2 新增 timer IRQ0 handler，最小行为为递增 tick counter，并在 validation-only 模式下输出有界 `BIGOS_TIMER_IRQ` marker。
- [x] 3.3 在 `initIRQ()` 或明确的 timer 初始化路径中接入 PIT init、timer handler registration 和 IRQ0 unmask。
- [x] 3.4 确保 IRQ0 handler 注册完成后才 unmask IRQ0，且 keyboard IRQ1 的既有注册先于 unmask 顺序不回退。
- [x] 3.5 确认 timer handler 不直接发送 i8259 EOI，仍由 C++ external IRQ dispatch 在 handler 返回后统一发送。
- [x] 3.6 保持 `BIGOS_MM_SELF_TEST` 在 PIT/PIC/timer 初始化和 `enableIRQ()` 之前运行。

## 4. 构建开关、Marker 与文档

- [x] 4.1 在 `xmake.lua` 中新增默认关闭的 timer smoke option，例如 `timer_smoke`。
- [x] 4.2 将 timer smoke option 映射为 `BIGOS_TIMER_SMOKE` 或等价宏，确保普通 boot 不输出周期性 timer marker。
- [x] 4.3 让 timer smoke marker 输出有界且稳定，例如只在第一个或前几个 tick 输出 `BIGOS_TIMER_IRQ`。
- [x] 4.4 更新相关架构文档或 roadmap 注记，说明unified boot handoff capability 只建立 PIT/tick foundation，不引入 scheduler、抢占或完整时间服务。

## 5. 源码级测试与静态检查

- [x] 5.1 新增或更新 `tests/` 源码级测试，检查 PIT port、base frequency、目标频率/divisor 常量可审查。
- [x] 5.2 新增或更新源码级测试，检查 timer IRQ0 handler 注册先于 IRQ0 unmask。
- [x] 5.3 新增或更新源码级测试，检查 timer handler 不直接调用 i8259 EOI，exception/IRQ EOI 边界不回退。
- [x] 5.4 新增或更新源码级测试，检查 memory self-test 仍位于 timer/PIC 初始化和 `enableIRQ()` 之前。
- [x] 5.5 新增或更新源码级测试，检查 timer smoke 由默认关闭的构建宏保护，marker 输出有界。
- [x] 5.6 运行相关 Python 测试时使用 `uv run pytest`；如果 `uv` 不可用，记录阻塞原因而不是静默使用系统 Python。

## 6. 构建、诊断与 Runtime 验证

- [x] 6.1 运行最窄有效的 `xmake` 或等价 `x86_64-elf-gcc/x86_64-elf-g++` 交叉构建，确认 C++ 和 assembly/driver 改动可构建。
- [x] 6.2 使用 clang 辅助命令检查修改过的 C++ 源/头文件；记录历史诊断、当前 change 新增诊断和 freestanding/toolchain false positive。
- [x] 6.3 使用 clangd 或 IDE diagnostics 检查修改过的 C++ 源/头文件，修复当前 change 引入的有效 error，并确认或修复有效 warning。
- [x] 6.4 在 Bochs 环境可用时运行普通 boot smoke，确认 timer 接入后仍能观察 normal boot marker。
- [x] 6.5 在 Bochs 环境可用且 `timer_smoke` 启用时运行 bounded serial smoke，例如 `uv run python tools/boot_debug.py run --expect-serial-marker BIGOS_TIMER_IRQ`。
- [x] 6.6 如果 Bochs、ROM、serial oracle 或 cross toolchain 无法运行，记录缺失依赖、已通过的替代检查和剩余 bootability/timer IRQ 风险。

## 7. 验收与归档准备

- [x] 7.1 运行 `openspec validate add-timer-irq-foundation --strict`，修复 proposal/design/spec/tasks 的结构问题。
- [x] 7.2 对照 `timer-irq-foundation` spec，确认每个 Requirement 至少有实现、测试或明确验证记录。
- [x] 7.3 对照 `interrupt-exception-foundation` 和 `kernel-memory-runtime-validation`，确认 EOI 边界和 memory self-test IRQ-disabled 边界未被破坏。
- [x] 7.4 汇总验证记录，分离已通过检查、无法运行检查及原因、历史诊断、当前 change 引入并已修复的问题、剩余风险。
