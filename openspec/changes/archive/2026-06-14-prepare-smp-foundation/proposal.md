## Why

BigOS 当前运行在明确的单核基线上，但后续真正启用 SMP 前必须先把锁模型、per-CPU 状态、调度边界、中断路由、TLB shootdown 与内存序假设显式化。现在整理这项 change 可以在不改变当前单核启动路径的前提下，降低未来多核执行引入时的初始化顺序、内存安全和中断安全风险。

## What Changes

- 引入 SMP 准备能力，定义真实多核执行启用前必须满足的内核级边界与验证标准。
- 分阶段整理锁与临界区规则，区分可在 IRQ 上下文使用的原语、调度器保护边界和内存管理保护边界。
- 建立最小 per-CPU 状态契约，用于描述当前 CPU 标识、当前线程/进程、内核栈/TSS 归属、IRQ 嵌套与抢占状态的未来承载位置。
- 明确单核调度器向 SMP 调度演进前的边界：当前默认仍只允许一个运行 CPU，跨 CPU 负载均衡与真正并行调度不在本 change 范围内。
- 记录中断路由、IPI、TLB shootdown 和内存序要求，作为后续切换到真实 SMP 的前置设计约束。
- 保持真实多核执行默认关闭；本 change 不引入 AP 启动、LAPIC/IOAPIC 完整驱动、跨核调度或 POSIX 级并发语义。

## Capabilities

### New Capabilities

- `smp-preparation`: 定义启用真实 SMP 前的锁模型、per-CPU 状态、单核兼容边界、中断路由假设、TLB shootdown 约束和内存序规则。

### Modified Capabilities

- 无。

## Impact

- 影响子系统：内核架构边界、IRQ/定时器路径、单核调度器、进程当前上下文、虚拟内存与页表失效路径、低层同步原语。
- 架构假设：目标仍为 x86_64 Legacy BIOS 长模式内核；本 change 可为 x86_64 SMP 设计铺路，但不启用真实 AP 执行。
- 内存布局假设：不改变内核高半区链接地址、直接映射、页表自映射、用户/内核 CR3 切换或现有 ABI 地址约定。
- 启动与硬件假设：当前 i8259/PIT/键盘/ATA PIO 与 QEMU/Bochs 单核验证路径保持有效；LAPIC、IOAPIC、IPI 与 AP bootstrap 只作为未来依赖记录。
- 工具链假设：继续使用 xmake、x86_64-elf-gcc、QEMU/Bochs 和 `uv run` Python helper；不引入 hosted runtime、线程库或外部依赖。
- 非目标：不实现 SMP 启动、不运行多个 CPU、不实现跨核调度、不改变 syscall ABI、不扩展 POSIX process/job-control 模型、不引入动态链接或新存储驱动。
