## Why

BigOS 已具备 AP startup、per-CPU local timer 和 per-CPU run queue 基线，但跨 CPU 的同步闭环仍不完整：远端 CPU 只能被调度 nudge，尚不能承载通用 IPI 投递、等待确认或页表变更后的 TLB shootdown。现在需要把既有 SMP preparation 契约推进到可执行状态，使多核调度、地址空间切换、VMA 解除映射、权限变更、COW 和共享只读映射都能在远端 CPU 仍可能运行同一地址空间时保持正确。

## What Changes

- 增加受控的 SMP IPI delivery 能力，支持向指定 online CPU 或 CPU 集合投递内核内部 IPI，并提供 bounded acknowledgement、timeout 和失败诊断。
- 启用 IRQ-safe locking 边界，使 IPI handler、TLB shootdown request/ack、scheduler nudge 和相关 interrupt-return 路径只使用 IRQ-safe、non-blocking 的同步原语。
- 将当前 TLB invalidation boundary 从单核 local invalidation 扩展为 cross-CPU shootdown：页表写入发布后，所有可能运行受影响 address space 的 online CPU 必须完成本地 invalidation 或被证明不需要 invalidation，调用方才可继续释放映射或返回用户态。
- 保留 scheduler nudge 的既有用途，但把它纳入通用 IPI 投递分类，明确它不是 TLB shootdown ack，也不能绕过 shootdown completion ordering。
- 保持 x86_64 Legacy BIOS 默认交付目标、现有 boot 地址、trampoline 低地址范围、linker 地址、IDT/syscall ABI、磁盘布局、用户态 ABI 和 UEFI spike 边界不变。
- 明确非目标：不实现 CPU hotplug、NUMA、完整负载均衡、实时调度、完整 APIC-backed 默认外部中断投递、广泛设备 IRQ 迁移、UEFI runtime parity、新 ISA/backend、完整 POSIX ABI 或 broad file-backed mmap 语义。

## Capabilities

### New Capabilities
- `smp-ipi-tlb-shootdown`: 描述 SMP 内部 IPI 投递、IRQ-safe shootdown handling、cross-CPU TLB invalidation completion、失败诊断和多核验证边界。

### Modified Capabilities
- `smp-preparation`: 将 IPI shootdown 和 cross-CPU TLB invalidation 从 future requirement 提升为本 change 明确启用的受控能力，同时保留 CPU hotplug、NUMA 和完整 APIC 默认中断投递为后续能力。
- `per-cpu-run-queues`: 将 scheduler nudge 归入通用 IPI delivery 的受控使用者，并明确调度 nudge 与 TLB shootdown 的 ordering、ack 和 validation 边界不同。
- `interrupt-exception-foundation`: 扩展 interrupt dispatch/EOI 约束以覆盖 SMP IPI vector 分类、IPI handler 的 IRQ-safe 执行边界，以及 syscall path、legacy IRQ path、LAPIC IPI path 的分离。
- `vma-user-memory-api`: 要求 unmap、protection change、address-space teardown、COW 或 fault recovery 导致的用户页表可见变更通过可完成的 TLB invalidation boundary 表达，且在 real SMP 下满足远端 CPU completion ordering。
- `shared-readonly-mappings`: 要求共享只读映射的跨进程 PTE removal、permission transition 和 frame reclaim 等路径使用 cross-CPU shootdown completion，而不是只依赖单核 local invalidation。

## Impact

- 影响子系统：`kernel/core/irq`、`kernel/core/sched`、`kernel/core/proc`、`kernel/mm`、x86_64 LAPIC/IPI 边界、per-CPU state、VMA/page-table invalidation 路径，以及相关 public headers。
- 架构假设：当前实现目标仍是 x86_64；依赖已上线 CPU 拓扑、AP startup、LAPIC EOI、per-CPU timer 与 per-CPU run queue 基线；不改变 AP trampoline 固定低地址区域或 boot handoff ABI。
- 内存与锁假设：IPI request state、shootdown target mask、ack state、run queue nudge state、current address-space tracking 和 page-table updates 必须使用 IRQ-safe lock、interrupt disable boundary、atomic operation 或 architecture fence 明确 ordering；hard IRQ context 不得执行可阻塞操作或普通动态分配。
- 工具与验证假设：优先使用 QEMU 多核 headless smoke 验证 IPI delivery、remote TLB shootdown completion 和 bounded userland baseline；Bochs 多核能力依赖本地构建，缺失时记录跳过项和残余风险；源码检查和 OpenSpec strict validation 必须覆盖 IRQ-safe locking 与 shootdown ordering。
