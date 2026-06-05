## Context

BigOS 当前已经能进入 long mode、高半区 kernel，完成 VGA/COM1 输出、内存初始化、可选 memory self-test、kernel-owned IDT、i8259 PIC remap、exception/IRQ 分流和 keyboard IRQ1 smoke。`kernel()` 末尾仍是 `hlt` 循环，没有 scheduler，也没有周期性时间源。

现有中断基础设施给 timer bring-up 提供了合适落点：

```text
kernel()
  -> vga::clear_screen()
  -> init_mem(boot_info)
  -> optional mm::self_test()      # 必须保持 IRQ disabled
  -> irq::initIRQ()
       -> build/load kernel IDT
       -> init i8259 PIC, mask all
       -> register early IRQ handlers
       -> unmask selected IRQ lines only after handlers are ready
  -> optional validation triggers
  -> irq::enableIRQ()
  -> boot reached marker
  -> hlt loop
```

本 change 将 timer 作为第二个 early external IRQ smoke 接入，顺序上与 keyboard IRQ1 并列，但 timer IRQ0 会周期性触发，因此必须避免无界输出、动态分配、阻塞等待或任何 scheduler 假设。

## Goals / Non-Goals

**Goals:**

- 新增 PIT/8253/8254 channel 0 初始化，配置一个明确的低频或中等频率周期性 tick。
- 注册 i8259 IRQ0 对应 vector `0x20` 的 timer handler，并在注册成功后才 unmask IRQ0。
- 维护单调递增 tick counter，提供可在 early kernel 使用的只读 tick API。
- 提供最小 busy-wait delay API，语义明确为不让出 CPU、不阻塞调度、不保证高精度。
- 新增默认关闭的 timer smoke marker，避免普通 boot 被周期性日志刷屏。
- 用源码级检查和可选 Bochs serial smoke 证明初始化顺序、EOI 边界和 marker 可观测性。

**Non-Goals:**

- 不实现 scheduler、kernel thread、抢占、时间片轮转或 idle thread。
- 不实现阻塞 `sleep`、wait queue、timer wheel、callout、deadline timer 或动态 timer queue。
- 不实现 APIC timer、IOAPIC、HPET、TSC calibration、tickless 或高精度时钟。
- 不实现 SMP/per-CPU tick、IPI、TLB shootdown 或跨 CPU 时间同步。
- 不实现用户态时间 syscall、进程计时或 POSIX time API。
- 不改变 boot 固定地址、内存布局、BootInfo ABI、direct map、`#PF` 诊断策略或 allocator 并发契约。

## Decisions

### Decision: 首版使用 PIT channel 0 + i8259 IRQ0

Timer foundation SHALL 使用 legacy PIT 8253/8254 channel 0 作为首个周期性时间源。PIT driver 使用显式端口常量：channel 0 data port `0x40`、mode/command port `0x43`，并配置 square wave/rate generator 等可在 Bochs 下稳定工作的模式。目标频率应以常量命名，例如 `TIMER_HZ`，分频值由 PIT base frequency `1193182 Hz` 推导。

选择 PIT 的原因是当前 kernel 已经处在 Legacy BIOS + i8259 bring-up 路线，PIT 与 IRQ0 能直接复用现有 PIC remap 和 external IRQ dispatch，不需要 APIC/HPET 枚举、ACPI 表解析或 calibration。

替代方案是直接引入 APIC timer 或 HPET。它们更接近现代硬件，但需要额外 discovery、MMIO、校准和中断控制器抽象，超出阶段 1 的基础目标。

### Decision: handler 注册先于 IRQ0 unmask

IRQ initialization SHALL 保持保守顺序：PIC init 后 mask all；timer handler 注册成功后才能 unmask `IRQ_LINE_TIMER`；keyboard IRQ1 的既有顺序不能回退。

```text
irq::initIRQ()
  -> init IDT / lidt
  -> init i8259, mask all
  -> init PIT hardware while IRQ0 still masked
  -> register timer IRQ0 handler
  -> unmask IRQ0
  -> register keyboard IRQ1 handler
  -> unmask IRQ1
```

实现可以选择把 PIT 初始化放在 `irq::initIRQ()` 内部，也可以由 `kernel()` 调用独立 `init_timer()`，但必须在 `enableIRQ()` 前完成，并通过源码级测试锁定“注册先于 unmask”的不变量。

### Decision: tick counter 是 early kernel 单调计数，不是完整时间服务

Timer handler SHALL 只做最小工作：递增一个单调 tick counter，并在 validation-only 模式下输出有界 marker。tick counter 可以使用对当前单核 x86_64 足够安全的整数类型；在没有 scheduler/SMP 的阶段，读取 API 只承诺在当前 early kernel 约束下返回单调 tick 快照。

`mdelay()`/`sleep()` 首版 SHALL 是 busy-wait primitive：它可以轮询 tick counter 或使用校准外的简单等待，但必须声明不让出 CPU、不进入阻塞队列、不依赖 scheduler，精度只满足 early driver smoke 和粗粒度延时。若无法在未启用 IRQ 的路径中使用，API 文档必须明确前置条件。

### Decision: timer smoke 默认关闭且输出有界

周期性 IRQ 如果每 tick 都输出，会显著扰乱串口日志和 boot smoke。因此 `BIGOS_TIMER_SMOKE` 或等价开关 SHALL 默认关闭；启用后只输出固定次数或特定 tick 的 marker，例如 `BIGOS_TIMER_IRQ`，以便 `tools/boot_debug.py --expect-serial-marker BIGOS_TIMER_IRQ` 观测。

普通 boot 仍应输出既有 normal boot marker，不应依赖 timer smoke marker 才算成功。timer smoke 是验证路径，不是运行时日志策略。

### Decision: EOI 继续由 external IRQ dispatch 统一发送

Timer handler MUST NOT 直接调用 i8259 EOI。现有 external IRQ dispatch 已经在 handler 返回后发送 EOI；timer IRQ0 应复用该边界，避免 handler 与 dispatch 双重 EOI 或 exception 路径误发 EOI。

Timer handler 也 MUST NOT 分配内存、阻塞等待、调用 filesystem、依赖 TTY、依赖 scheduler、访问用户态或使用 hosted runtime API。

### Decision: memory self-test 仍在 timer/PIC/IRQ enable 之前

已有 memory runtime validation 只承诺单核、早期关中断环境。`BIGOS_MM_SELF_TEST` 仍必须在 timer/PIC 初始化和 `enableIRQ()` 之前运行，timer change 不得借由 tick counter 扩展 allocator 的 IRQ-context 安全语义。

## Risks / Trade-offs

- [Risk] PIT 配置错误导致 IRQ0 不触发或频率异常 -> Mitigation: 用显式常量和源码级测试检查 divisor/port/mode，并用 Bochs serial marker smoke 验证。
- [Risk] timer 周期性输出刷爆 serial log -> Mitigation: smoke 默认关闭，启用后只输出有界 marker。
- [Risk] unmask IRQ0 过早导致默认 handler 路径持续触发 -> Mitigation: 保持 handler 注册先于 unmask，并增加源码级检查。
- [Risk] busy-wait delay 被误认为 scheduler sleep -> Mitigation: API 命名、注释和 spec 明确其 busy-wait 语义与前置条件。
- [Risk] tick counter 读取在未来 SMP/抢占下不够强 -> Mitigation: 本 change 只声明当前单核 early kernel 语义，后续 scheduler/SMP change 必须重新定义并发契约。
- [Risk] Bochs/ROM/serial oracle 在本地不可用 -> Mitigation: 记录无法运行原因、已通过的 source/build 检查和剩余 runtime 风险。

## Migration Plan

1. 复查现有 IRQ/PIC/keyboard 初始化顺序，确认 timer IRQ0 的注册点。
2. 新增 PIT driver header/source，定义端口、base frequency、目标频率和 divisor 计算。
3. 新增 timer state/API：tick counter、`ticks()`、busy-wait delay primitive、可选 smoke emission。
4. 在 IRQ 初始化中接入 PIT init、timer handler registration、IRQ0 unmask。
5. 新增 xmake validation 开关并映射到 `BIGOS_TIMER_SMOKE` 或等价宏。
6. 增加源码级测试覆盖 PIT 常量、注册/unmask 顺序、EOI 边界、memory self-test 顺序和 handler freestanding-safe 约束。
7. 运行交叉构建、clang/clangd 辅助诊断和可用的 Bochs serial smoke。
8. 更新架构文档或 roadmap 注记，说明 timer foundation 不等于 scheduler 或完整时间子系统。
