## Why

BigOS 已完成单核兼容的 SMP preparation 边界；下一步需要把 application processor 启动、APIC-backed 中断入口和 per-CPU timer 从“未来依赖”推进为可验证的 x86_64 多核启动基础。该 change 先建立受控的 AP bring-up 和本地定时器能力，为后续跨 CPU 调度、IPI/TLB shootdown 与默认 APIC 中断投递提供硬件基础。

## What Changes

- 引入 x86_64 application processor 启动能力，覆盖 CPU 枚举、AP trampoline、INIT/SIPI 启动序列、AP 长模式进入、per-CPU 状态绑定和 AP 在线确认。
- 引入 LAPIC/IOAPIC 初始化边界，使 BSP 与 AP 能使用 APIC-backed 中断控制路径，同时保留当前 Legacy BIOS 默认交付目标和明确的回退/失败策略。
- 引入 per-CPU timer 基线，使每个已上线 CPU 具备本地调度 tick 来源，并明确与现有 PIT 单核 tick 的过渡关系。
- 更新 SMP preparation 合同：APIC、AP startup 与 per-CPU timer 不再只是未来依赖，而是本 change 的受控启用范围；跨 CPU 调度、IPI shootdown 和完整多核吞吐仍留给后续 change。
- 约束启动、内存、IRQ 和调度初始化顺序，确保 AP 启动不会改变 syscall ABI、用户态进程模型、磁盘布局、页表布局或现有 bounded userland 语义。

## Capabilities

### New Capabilities

- `ap-startup-percpu-timers`: 定义 x86_64 AP 启动、APIC 控制器初始化、AP 长模式进入、per-CPU 状态上线和 per-CPU timer 基线。

### Modified Capabilities

- `smp-preparation`: 将 LAPIC/IOAPIC、AP startup 和 per-CPU timer 从 future dependency 调整为受控启用能力，同时保留跨 CPU 调度、IPI shootdown 和完整 SMP 调度为后续范围。
- `interrupt-timer-context-boundary`: 扩展中断/定时器边界以覆盖 APIC-backed interrupt delivery 与 per-CPU timer，不再把 APIC/IOAPIC 全部列为本边界之外。

## Impact

- 影响子系统：x86_64 boot/arch 初始化、CPU 枚举、低地址 AP trampoline、GDT/TSS/per-CPU 状态、IRQ 控制器初始化、timer tick、scheduler tick 入口和早期诊断路径。
- 架构假设：当前实现范围仍限定为 x86_64；不新增非 x86 ISA，不要求 UEFI runtime parity，也不把 UEFI backend 提升为默认运行时等价 backend。
- 内存布局假设：需要为 AP trampoline 保留受控低地址执行区域，但不得静默改变内核高半区链接地址、direct map、页表自映射、用户/内核 CR3 切换约定或磁盘布局。
- 启动与硬件假设：默认 Legacy BIOS 路径保持交付基线；APIC/IOAPIC 可作为多核启动所需硬件路径启用，缺失或异常时必须 fail closed 或回退到明确的单核诊断路径。
- 工具链与验证假设：继续使用 xmake、x86_64-elf-gcc、QEMU/Bochs 和 `uv run` Python helper；运行时验证优先覆盖 QEMU headless 的多核配置，Bochs/APIC 行为可作为补充交叉验证。
- 非目标：不实现 per-CPU run queue、跨 CPU work migration、负载均衡、IPI-backed TLB shootdown、CPU hotplug、NUMA、完整 APIC 中断重路由策略、动态链接、完整 POSIX 进程模型或新存储/设备 ISA。
