## Context

当前多核相关规格已经覆盖 AP startup/per-CPU timer、per-CPU run queue、类型化 IPI 与 TLB shootdown 的功能合同。本变更不重新定义这些基础能力，而是在它们之上建立加固层：用可复现压力验证发现竞态，用显式锁顺序约束调度/内存/中断共享状态，用确定性诊断和 fail-closed 策略处理 AP、IPI、timer、shootdown 的故障和超时。

受影响的控制流跨越 APIC 启动与 EOI、timer IRQ、scheduler remote enqueue、IPI handler、VM page-table 更新、`mm context` residency、panic/serial/VGA 诊断输出。实现时必须保持 freestanding C++17/C17 约束，不改变 kernel link address、direct map、page-table self-map、AP trampoline 低地址保留、IDT/syscall vector、磁盘布局或用户 ABI。

## Goals / Non-Goals

**Goals:**

- 建立多核压力与回归验证入口，覆盖调度、跨核唤醒、IPI 投递、TLB shootdown 完成和 baseline userland 回归。
- 明确 scheduler domain、IPI request、TLB shootdown、`mm context` residency、CPU online/offline/fail state 之间的锁顺序和等待规则。
- 将 AP startup、remote wakeup、IPI ack、shootdown timeout、timer 异常等故障统一收敛到可诊断、有限等待、fail-closed 的路径。
- 记录工具缺失时的替代检查和残余风险，避免把未跑过的仿真路径描述为已验证。

**Non-Goals:**

- 不实现 CPU hotplug、NUMA、完整负载均衡、抢占式实时调度或完整 POSIX 进程/作业控制模型。
- 不扩展用户态 ABI，不引入新的系统调用语义。
- 不实现异步 I/O、virtio/AHCI/NVMe 等新设备驱动，且不要求 UEFI backend runtime parity。
- 不改变启动地址、链接地址、页表布局、AP trampoline 布局、磁盘镜像布局或 interrupt vector 分配。

## Decisions

1. 采用独立的多核加固 smoke，而不是复用单一功能 smoke。

   理由：单项 smoke 能证明功能入口可达，但很难覆盖远程唤醒与 shootdown 同时存在时的发布顺序、锁顺序和超时诊断。独立 smoke 默认固定为 2 CPU，以覆盖最小跨核交互并保持日常回归稳定；测试脚本允许覆盖到 4 CPU，用于增强 target set、ack bitmap、锁顺序和多个 AP 同时响应的压力验证。smoke 还应固定迭代次数、串口输出和超时边界，并在结束时验证默认 userland baseline 没有回退。

   备选方案是只增加源代码审计清单。该方案成本低，但不能暴露 AP timer、IPI delivery、IRQ-return scheduling 和 TLB invalidation 的真实跨 CPU交互。

2. 锁顺序以“短持有、不可阻塞、等待前释放”为基本规则。

   scheduler run queue lock 只保护本 CPU 或目标 CPU 的 runnable/sleep/current 状态；IPI request/shootdown completion 状态只允许 IRQ-safe 更新；`mm context` residency 保护地址空间活跃 CPU 集合。任何需要等待远程 CPU ack 的路径不得持有 run queue lock，也不得依赖 scheduler-managed blocking primitive 完成 IPI handler 工作。

   备选方案是引入全局 SMP 大锁。它能降低早期实现复杂度，但会掩盖真实锁顺序问题，并让未来 I/O 或 VM 路径更难拆分。

3. 故障路径 fail-closed 优先于继续运行，并以 timer tick 作为默认超时阈值。

   AP 未按时上线时保持 offline/failed，不能进入 scheduler target set 或 shootdown target set。TLB shootdown required target 超时必须绑定默认 timer tick 阈值，超过阈值后阻止 frame reclaim、address reuse 或 user-mode return。IPI 投递到非法 CPU 必须被拒绝或显式排除，不能无限等待。

   备选方案是使用 LAPIC-independent bounded spin 或超时后仅打印警告并继续。前者能减少对 timer interrupt 的依赖，但阈值更容易受宿主机速度和仿真器差异影响；后者会把 stale TLB、重复 runnable、失败 AP 等错误转化为更晚出现的内存破坏或不可复现崩溃。

4. 诊断输出保持当前内核诊断边界，并统一为一个小型诊断结构。

   多核故障应通过现有 serial/VGA/panic 诊断路径输出一个小型统一诊断结构，字段覆盖 CPU id、APIC id、vector、target set、generation/ack state、timeout kind 等最小必要信息。结构和打印 helper 必须保持 panic-safe、allocation-free，不得绕过已有 `io.cc` 保护机制直接访问 VGA；在用户 CR3 下输出诊断时仍必须保持 CR3 安全。

   备选方案是为每个子系统添加分散的临时打印。该方案实现快，但难以在超时和 panic 中保证一致字段，也容易破坏用户地址空间下的诊断安全。

## Risks / Trade-offs

- [Risk] 压力 smoke 在不同 QEMU/Bochs 版本或宿主机性能下出现偶发超时 → Mitigation：使用有界迭代、显式超时原因、串口 marker 和可配置 CPU 数；验证记录必须区分真实失败与工具不可用。
- [Risk] 过早扩大锁抽象导致中断上下文误用 allocator 或 blocking primitive → Mitigation：先定义锁顺序和 IRQ-safe 禁止项，再实现最小封装；源码检查覆盖 IPI/timer handler。
- [Risk] fail-closed panic 会降低交互式调试便利性 → Mitigation：panic 前输出目标 CPU、generation、ack bitmap 和当前 CPU 状态，使失败点可复现。
- [Risk] 多核加固可能暴露既有单核路径对 BSP singleton 的隐式依赖 → Mitigation：保留 BSP-only fallback，并要求多核开关关闭时默认 userland baseline 仍可运行。

## Migration Plan

1. 增加/调整多核验证 build switch 与 smoke 入口，默认关闭，不影响默认启动。
2. 审计 scheduler、IPI、VM shootdown、AP startup/timer 的锁顺序和 IRQ-context 限制，先修复会导致死锁或无限等待的路径。
3. 增加 fail-closed 诊断字段和超时处理，再接入压力 smoke。
4. 运行源检查、普通构建、QEMU headless 多核 smoke；Bochs 作为 APIC/早期启动交叉验证可用时再执行。
5. 若回退，关闭新增 smoke 开关并保留默认 BSP/userland baseline；不得回退无关多核基础能力。

## Open Questions

暂无。
