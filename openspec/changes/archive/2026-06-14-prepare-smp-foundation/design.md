## Context

BigOS 当前内核运行模型是单核 x86_64 Legacy BIOS 路径：引导后进入长模式，使用内核自有 IDT、i8259/PIT 中断、单核 round-robin 调度器、显式 CR3 切换、VMA 支持的用户内存访问、buddy/slab/kmalloc 内存分配，以及默认 PID-1 init 与 `/bin/sh` 用户态基线。Stage 38 的目标不是立即运行多个 CPU，而是在仍保持单核验证路径稳定的前提下，把未来 SMP 所依赖的边界显式整理出来。

这项设计跨越 boot/arch、IRQ、timer、sched、proc 和 mm 边界。它必须保留现有启动地址、链接地址、IDT/syscall vector、页表布局、磁盘布局、syscall ABI 和默认 smoke 语义；任何真实 AP bring-up、LAPIC/IOAPIC 切换、IPI 投递或跨核调度都只能作为后续 change 的依赖。

## Goals / Non-Goals

**Goals:**

- 定义启用真实 SMP 前必须具备的锁模型与中断上下文约束，避免在 IRQ-return 抢占、等待队列、fd/VFS、进程生命周期和内存管理路径中引入隐式多核假设。
- 建立最小 per-CPU 状态布局契约，描述 CPU 标识、当前线程/进程、当前地址空间、内核栈/TSS、IRQ 嵌套、抢占关闭深度和本地统计的归属。
- 明确调度器从单核运行队列向 SMP 演进前的保护边界，并保持默认只运行 bootstrap CPU。
- 记录 TLB shootdown 与内存序需求，确保后续跨核页表修改、COW、地址空间销毁和 CR3 切换不会依赖未定义的失效顺序。
- 提供单核基线可验证的准入条件：所有新增抽象在一个 CPU 上必须退化为现有行为，且不改变 boot、syscall、userland 和 runtime smoke 的外部语义。

**Non-Goals:**

- 不启动 AP，不实现 SIPI trampoline，不引入真实多核执行。
- 不把 i8259/PIT 默认路径替换为完整 LAPIC/IOAPIC 驱动。
- 不实现跨核调度、负载均衡、CPU hotplug、NUMA、RCU 或抢占式内核线程并行执行。
- 不改变 syscall ABI、用户态进程模型、POSIX job-control/session/process-group 语义或动态链接能力。
- 不改变内核高半区链接地址、直接映射、页表自映射、磁盘布局或现有 interrupt vector 分配。

## Decisions

### Decision: 先引入 SMP 准备契约，不启用真实 SMP

采用“单核兼容的 SMP-ready 边界”作为本 change 的输出，而不是直接启用多个 CPU。这样可以先审计锁、per-CPU、TLB 和调度边界，避免在 AP bring-up 后才发现单核全局变量、IRQ 路径和页表失效语义互相冲突。

替代方案是直接实现 AP 启动和 LAPIC timer，再边跑边修锁。该方案会同时改变 boot、IRQ、timer、sched 和 mm 行为，调试面过大，不适合当前阶段。

### Decision: 锁模型按上下文能力分层

锁模型划分为普通内核上下文锁、IRQ-safe 锁和未来 per-CPU 本地保护。普通锁不得在硬 IRQ 路径中阻塞；IRQ-safe 锁必须明确本地中断保存/恢复语义；per-CPU 本地保护只能保护当前 CPU 私有状态，不能被误用为跨 CPU 互斥。

替代方案是统一使用一个 spinlock 类型覆盖所有场景。该方案接口简单，但会隐藏 IRQ 嵌套、调度器重入、等待队列和内存分配路径的不同约束。

### Decision: per-CPU 状态先成为显式访问边界

per-CPU 状态需要先描述“哪些全局当前状态未来必须按 CPU 拆分”，包括当前线程/进程、当前地址空间、内核栈/TSS、IRQ 嵌套、抢占关闭深度和本地调度标志。单核实现可以仍映射到 bootstrap CPU 的静态槽，但调用方不得再假设这些状态是进程全局或系统全局单例。

替代方案是在真正 SMP 时一次性改造所有全局状态。该方案容易遗漏隐式依赖，并且会让调度器、syscall、page fault 与 user copy 的 CR3/当前进程关系难以验证。

### Decision: TLB shootdown 先定义协议与降级语义

页表修改、地址空间销毁、COW 权限变更和用户 VMA teardown 必须具备一个明确的 TLB 失效接口。当前单核实现可以退化为本地 `invlpg` 或 CR3 reload；未来 SMP 实现必须在接口下补充目标 CPU 集合、IPI 投递、ack 等待和失败处理。

替代方案是在每个 mm 调用点直接执行本地失效。该方案适合单核，但会把未来跨核 shootdown 需求分散到多个路径，增加内存安全风险。

### Decision: 中断路由假设作为独立前置条件记录

在真实 SMP 前，必须明确哪些中断仍由 bootstrap CPU 处理，哪些未来可迁移到 LAPIC/IOAPIC 或 per-CPU timer。当前 i8259/PIT 路径保持默认不变，IPI 和 APIC 只作为设计依赖，不进入默认运行路径。

替代方案是同时迁移中断控制器。该方案会让“准备工作”和“硬件后端切换”耦合，难以用现有 QEMU/Bochs 单核 smoke 验证。

## Risks / Trade-offs

- [Risk] 只整理边界可能看起来没有可见功能增量 -> Mitigation: 以可验证的单核退化行为、接口边界和文档化约束作为完成标准。
- [Risk] 锁原语过早泛化会增加调用点复杂度 -> Mitigation: 只引入当前能被单核路径验证的最小 API，避免完整并发库。
- [Risk] per-CPU 抽象若隐藏过多会掩盖初始化顺序 -> Mitigation: bootstrap CPU 的初始化顺序、默认槽和失败行为必须显式记录。
- [Risk] TLB shootdown 接口在单核阶段缺少真实跨核验证 -> Mitigation: 单核路径必须覆盖本地失效，跨核投递与 ack 留到后续真实 SMP change。
- [Risk] IRQ-safe 锁与调度器抢占控制混用可能导致死锁 -> Mitigation: 明确 IRQ 路径不得阻塞，调度器路径必须保持 allocation-free 和重入边界。

## Migration Plan

- 先审计现有全局当前状态、调度器队列、mm 页表失效和 IRQ 路径，标记哪些位置需要通过 SMP 准备接口访问。
- 引入单核兼容的同步、per-CPU 和 TLB 失效边界，默认映射到 bootstrap CPU，不改变外部 ABI 或 smoke marker。
- 逐步将调度器、proc、syscall/page fault 和 mm 调用点迁移到新边界，并保留单核行为。
- 完成后运行窄构建和相关默认/可选 smoke；若失败，可回退到原单核全局路径，因为本 change 不要求启用真实 AP。

## Resolved Follow-Up Decisions

- 后续真实 SMP change 优先引入 LAPIC timer，而不是长期保持 BSP timer 作为唯一调度 tick 来源。IPI/shootdown 仍是必要能力，但真实 SMP 方向应尽早建立 per-CPU timer 基线。
- per-CPU 存储采用两阶段方案：第一阶段使用固定 `per_cpu[MAX_CPUS]` 数组和 `current_cpu_id()`/`current_cpu()` 访问边界，少数早期初始化路径允许显式 `cpu_id`；第二阶段在真实 SMP 稳定后将底层实现切换到 GS base，同时保持上层 API 不变。
- 调度器采用每 CPU run queue 加极简迁移策略作为第一版 SMP 模型。默认 runnable work 进入当前 CPU 或明确目标 CPU 队列，跨 CPU wakeup 通过最小 IPI nudging 触发，暂不实现完整负载均衡。

## Open Questions

- LAPIC timer 的校准策略、tick 频率和与现有 PIT 路径的过渡关系需要在真实 SMP change 中进一步细化。
- 第一版每 CPU run queue 是否需要保留全局 fallback queue，以及 fallback queue 的使用条件，需要结合实现复杂度和验证结果决定。
