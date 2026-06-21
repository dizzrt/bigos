## Context

BigOS 的当前多核基线已经覆盖 CPU 拓扑、AP startup、LAPIC/per-CPU timer 和 per-CPU run queue。调度器可以把 runnable work 放到远端 CPU 并通过 scheduler nudge 促使远端观察本地队列，但这个机制仍是调度专用边界：它不提供通用 IPI 类型分类、远端 handler acknowledgement、跨 CPU TLB invalidation completion，也不能证明页表变更对正在运行同一 address space 的远端 CPU 已经生效。

本 change 连接 IRQ、scheduler、process VM 和 x86_64 LAPIC 边界。实现必须保持 freestanding-safe，继续以 x86_64 Legacy BIOS 路径为默认交付目标，并保留 UEFI spike 的非等价 backend 状态。实现不改变 boot addresses、linker addresses、AP trampoline 固定低地址区域、IDT/syscall ABI、disk layout、page-table self-mapping 或用户态 ABI。

## Goals / Non-Goals

**Goals:**

- 提供 bounded SMP IPI delivery，用于 scheduler nudge 和 TLB shootdown 两类明确的内核内部消息。
- 为 IPI handler、shootdown request、ack state、interrupt-return path 和 page-table invalidation 建立 IRQ-safe、non-blocking 的锁与 ordering 规则。
- 将 TLB invalidation boundary 扩展为 cross-CPU shootdown：页表更新发布后，所有可能运行受影响 address space 的 online CPU 都完成本地 invalidation 或被排除在目标集合之外，调用方才可释放映射、复用 frame 或返回用户态。
- 引入独立 `mm context` 引用计数与 CPU residency tracking，使进程地址空间生命周期不再只依赖 current process 指针或隐式 CR3 扫描。
- 保持 BSP-only 或 SMP-disabled fallback 可运行，单核场景仍退化为 local invalidation。
- 提供专用 TLB shootdown smoke build switch，并覆盖源码不变量、OpenSpec strict validation、QEMU 多核 smoke；Bochs 或本地工具缺失时明确记录残余风险。

**Non-Goals:**

- 不实现 CPU hotplug、NUMA、完整 APIC-backed 默认外部中断投递或广泛设备 IRQ 迁移。
- 不改变 AP startup trampoline 范围、boot handoff ABI、linker layout、IDT vector ABI、syscall ABI 或用户可见 POSIX 语义。
- 不引入 broad file-backed `mmap`、shared writable mapping、swap、动态链接、完整 libc 或新 ISA/backend。
- 不把 scheduler nudge 等同于 TLB shootdown；两者可共享 IPI 投递基础，但 completion、ack 和 ordering 独立。

## Decisions

1. IPI delivery 采用类型化、内核内部 API，而不是暴露通用用户态或设备可见接口。

   Rationale: 当前只需要 scheduler nudge 与 TLB shootdown。类型化接口可以固定 handler 执行上下文、ack 语义和超时诊断，避免过早承诺完整 generic IPI routing。

   Alternatives considered: 直接复用 scheduler nudge 作为所有远端通知。该方案缺少 target set、ack state 和 failure reporting，无法表达 TLB shootdown 的 completion ordering。

2. TLB shootdown request 在普通内核上下文准备，在 hard IRQ handler 中只执行本地 invalidation 与 ack。

   Rationale: shootdown 可能需要构造目标 CPU 集合、检查 address-space residency 和等待远端确认，这些动作应在可控上下文完成。IPI handler 必须 allocation-free、non-blocking，避免在 IRQ context 中进入 VM、VFS 或 scheduler blocking 路径。

   Alternatives considered: 让 IPI handler 动态查找并修正复杂 VM 状态。该方案会把可阻塞或可分配路径带入 IRQ context，违反 SMP preparation 的锁分类。

3. 目标 CPU 集合由 address-space residency 与 CPU online/schedulable 状态共同决定。

   Rationale: 只有可能正在运行、或可能在返回用户态前继续使用受影响 CR3/address space 的 CPU 需要 shootdown。offline、failed、未参与调度或能通过 CR3 switch 自然失效的 CPU 应被明确排除，避免无界等待。

   Alternatives considered: 对所有 discovered CPU 广播 shootdown。该方案简单但会把 offline/failed AP 纳入等待集合，增加超时和误报风险。

4. Page-table update 必须先发布，再触发 shootdown；frame reclaim 或权限依赖动作必须等 shootdown completion 之后执行。

   Rationale: 远端 CPU 看到旧 TLB entry 时，调用方不能提前释放 frame 或放宽权限。这个顺序需要由锁、atomic/fence 或 interrupt-disable boundary 明确表达。

   Alternatives considered: 先发送 IPI 再修改 PTE。该方案无法保证远端 invalidation 覆盖最终 PTE 状态，可能留下陈旧 translation。

5. 使用 bounded timeout 和 fail-closed 诊断处理 shootdown ack 缺失。

   Rationale: 研究内核需要可诊断失败，而不是无限等待。超时意味着目标 CPU 状态、IPI delivery 或 IRQ handling 出现严重不一致；应进入受控 panic/diagnostic path 或停止继续释放危险资源。

   Alternatives considered: 忽略未 ack CPU 并继续执行。该方案会破坏页表/Frame 生命周期安全。

6. Address-space residency tracking 通过独立 `mm context` 引用计数实现，而不是仅绑定到 current process/address space。

   Rationale: TLB shootdown 的正确目标不是“当前进程对象”本身，而是可被 CPU 继续使用的 address-space root。独立 `mm context` 可以承载 page-table root、引用计数、active CPU mask、teardown 状态和 shootdown generation，避免后续 thread/process/mm 分离时再次重构 shootdown 目标选择。

   Alternatives considered: 每个 CPU 只记录 current process 或 current address-space root，shootdown 时扫描 online CPU。该方案实现更小，但会把 address-space lifetime、process lifetime 和 CPU residency 绑在一起，不利于 fork/exec/teardown、kernel thread 借用地址空间、以及未来更清晰的 VM ownership。

7. Shootdown runtime validation 使用专用 build switch，而不是复用现有 userland/VM smoke 作为主验证入口。

   Rationale: IPI delivery、remote ack、timeout、TLB invalidation ordering 和 frame reclaim ordering 是低层同步能力，失败时需要独立 marker 和最小复现路径。专用 switch 可以保持验证范围可控，也避免现有 userland smoke 因多核 shootdown 压力变得过重。

   Alternatives considered: 在既有 userland 或 VM smoke 中插入内部 shootdown marker。该方案适合作为集成回归补充，但不适合作为首个定位 IPI/shootdown 失败的主验证入口。

## Risks / Trade-offs

- [Risk] IPI vector 与现有 exception、legacy IRQ、syscall vector 冲突 -> Mitigation: 固定在内核私有 LAPIC IPI vector 分类内注册，保持 `int 0x80`、CPU exception 和 i8259 remap path 不变，并通过源码检查覆盖 vector 分类。
- [Risk] Shootdown wait 在持有错误锁时造成死锁 -> Mitigation: 规定等待远端 ack 前不得持有会被 IPI handler、scheduler nudge 或 VM fault path 反向获取的锁；任务中加入锁顺序审查。
- [Risk] AP 在 interrupt disabled 或 preemption-disabled 过久时延迟 ack -> Mitigation: 只承诺 bounded diagnostic，不承诺实时延迟；验证覆盖多核用户态 baseline 和压力式 VMA/permission transition。
- [Risk] 引入 `mm context` 引用计数扩大进程/VM 重构面 -> Mitigation: 先限制 `mm context` 为 address-space root、refcount、active CPU mask 和 teardown 状态的最小所有权对象，不引入完整 POSIX mm、线程组、shared writable mapping 或动态链接语义。
- [Risk] Bochs 本地多核能力不稳定 -> Mitigation: QEMU 作为首选多核 smoke；Bochs 只作为可用时的交叉验证，缺失时记录跳过项和残余风险。
- [Risk] 单核 fallback 被 SMP 路径复杂化 -> Mitigation: 保留 explicit SMP-disabled/local invalidation path，源码检查确认 BSP-only boot 不依赖 AP 或 IPI。

## Migration Plan

1. 建立 IPI vector 分类、handler 注册和 LAPIC send/EOI 边界，先保持 scheduler nudge 语义不变。
2. 引入最小 `mm context` 对象，承载 address-space root、引用计数、active CPU mask、teardown 状态和 shootdown generation，并把进程 exec/fork/exit 的地址空间所有权迁移到该对象。
3. 引入 IRQ-safe request/ack 数据结构与 lock ordering，完成 source-level 检查。
4. 扩展 TLB invalidation boundary，单核 path 继续 local invalidation，多核 path 基于 `mm context` residency 构造 target CPU set 并等待 ack。
5. 将 VMA unmap、mprotect/protection change、address-space teardown、COW transition 和 shared read-only removal 接入新的 invalidation boundary。
6. 增加专用 TLB shootdown smoke build switch，并用 QEMU 多核 smoke 验证 IPI delivery、shootdown completion 和 bounded userland baseline；无法运行的 emulator 记录跳过原因。
7. 回滚策略：保留 build-time 或 runtime SMP-disabled fallback；若 IPI/shootdown 不稳定，可退回 local-only invalidation 并禁止 AP 参与 user address-space execution；若 `mm context` 生命周期异常，禁止共享该 address-space root 的多 CPU residency。

## Open Questions

- 无。当前设计明确采用专用 TLB shootdown smoke build switch，并引入独立 `mm context` 引用计数作为 address-space residency tracking 的基础。
