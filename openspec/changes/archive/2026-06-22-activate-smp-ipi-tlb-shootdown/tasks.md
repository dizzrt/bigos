## 1. IPI 与中断边界

- [x] 1.1 盘点现有 LAPIC、interrupt dispatch、scheduler nudge 和 AP timer 路径，确认可复用的 IPI vector、EOI 和 per-CPU state 边界
- [x] 1.2 实现 typed SMP IPI delivery API，覆盖 scheduler nudge 与 TLB shootdown 两类内部消息，并保持用户态 ABI 不变
- [x] 1.3 将 IPI vector 分类接入 interrupt dispatch，确保 IPI path 使用 LAPIC EOI，且不影响 CPU exception、i8259 IRQ 和 syscall vector
- [x] 1.4 为 IPI request、target mask、delivery state 和 ack state 增加 deterministic timeout/failure diagnostics

## 2. IRQ-safe 锁与 ordering

- [x] 2.1 定义并实现 IPI/shootdown 所需 IRQ-safe synchronization 边界，避免 hard IRQ path 使用 blocking wait、普通动态分配、VFS 或 block I/O
- [x] 2.2 审查 scheduler domain、IPI request state、TLB shootdown state 和 VM/page-table locks 的获取顺序，记录并修复潜在死锁
- [x] 2.3 为 page-table update、IPI request publication、remote ack 和 frame reclaim 增加明确的 memory ordering 或 interrupt boundary
- [x] 2.4 确认 BSP-only 和 SMP-disabled fallback 仍退化为 local invalidation，不要求 AP startup 或 IPI delivery

## 3. Cross-CPU TLB Shootdown

- [x] 3.1 引入最小 `mm context` 对象，承载 address-space root、引用计数、active CPU residency、teardown 状态和 shootdown generation
- [x] 3.2 将 exec、fork、exit/reap 和 address-space teardown 的 VMA/page-table ownership 迁移到 `mm context` 引用计数模型
- [x] 3.3 在 CPU address-space switch 路径维护 `mm context` active CPU residency，确保 target set 不依赖隐式 current process 扫描
- [x] 3.4 扩展 TLB invalidation boundary，使调用方能传入 `mm context`、address range/page、reason、target CPU set 和 completion requirement
- [x] 3.5 基于 online CPU 与 `mm context` residency 构造 shootdown target set，排除 offline/failed/non-resident CPU 并保留诊断信息
- [x] 3.6 实现 TLB-shootdown IPI handler，限制其只执行本地 invalidation、IRQ-safe ack 和 LAPIC EOI
- [x] 3.7 实现 requester-side completion wait，确保缺失 ack 时 fail closed，且不得继续释放依赖 shootdown 的 frame 或 page-table page

## 4. VM 与共享映射接入

- [x] 4.1 将匿名 unmap、protection change、address-space teardown 和 exec replacement 的 PTE removal 接入新的 invalidation boundary
- [x] 4.2 将 COW write fault、demand materialization 和 permission transition 的 stale translation 处理接入 cross-CPU shootdown ordering
- [x] 4.3 将 shared read-only mapping 的 PTE removal、metadata removal 和 frame reference release 接入 shootdown completion
- [x] 4.4 审查 inactive address-space teardown 优化，确保任何避免 IPI 的路径都有显式 residency 或 CR3 switch 依据

## 5. 调度 nudge 集成

- [x] 5.1 将 remote scheduler nudge 切换为 typed IPI delivery consumer，同时保持 per-CPU run queue 语义与 runnable publication order
- [x] 5.2 确认 scheduler nudge ack/state 不被 VM shootdown 当作 TLB invalidation completion
- [x] 5.3 验证 IRQ-return preemption 仍只访问当前 CPU scheduler domain，不回退到 BSP-only scheduler state

## 6. 验证与文档记录

- [x] 6.1 增加专用 TLB shootdown smoke build switch，使用默认关闭策略并输出可区分 IPI delivery、`mm context` residency 和 shootdown completion 的验证标记
- [x] 6.2 运行 OpenSpec strict validation，确认 proposal、design、spec delta 和 tasks 可解析且不含阶段编号依赖
- [x] 6.3 运行窄范围 xmake cross-toolchain build；若 x86_64-elf-gcc/xmake 不可用，记录 blocker、替代检查和残余风险
- [x] 6.4 对修改过的 C++ source/header 执行 clang 与 clangd 辅助诊断；区分历史诊断、当前变更引入诊断和 freestanding 配置 false positive
- [x] 6.5 运行 QEMU 多核 headless smoke，观察专用 TLB shootdown smoke switch 下的 IPI delivery、`mm context` residency、remote TLB shootdown completion 和 bounded userland baseline；若 QEMU 不可用，记录跳过原因
- [x] 6.6 在本地 Bochs 多核能力可用时执行交叉 smoke；若 Bochs 不支持多核或显示/ROM 配置不可用，记录跳过原因和残余风险
- [x] 6.7 更新实现验证记录，列出已通过检查、未运行检查、toolchain/emulator 约束、IRQ-safe locking 审查结果和 shootdown ordering 结论
