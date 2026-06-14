## 1. 边界审计

- [x] 1.1 审计当前全局 CPU/线程/进程/地址空间状态，列出必须迁移到 CPU-local 边界的调用点。
- [x] 1.2 审计 IRQ、timer、syscall、page fault 和 IRQ-return preemption 路径，标记硬 IRQ 中不得阻塞或动态分配的边界。
- [x] 1.3 审计 scheduler ready queue、wait queue、sleep list、idle thread 和 reschedule flag 的所有权假设。
- [x] 1.4 审计 mm 页表修改、COW、地址空间销毁和 CR3 切换路径，标记需要统一 TLB invalidation 边界的位置。

## 2. 同步与 Per-CPU 基础

- [x] 2.1 定义最小同步原语分类，明确普通内核上下文锁、IRQ-safe 锁和 CPU-local 保护的适用范围。
- [x] 2.2 引入 bootstrap-only per-CPU 状态访问边界，覆盖 CPU id、当前线程/进程、当前地址空间、IRQ 嵌套和抢占关闭深度。
- [x] 2.3 将已审计的当前执行状态读取路径迁移到 per-CPU 访问边界，并保持单核行为不变。
- [x] 2.4 为非 bootstrap CPU 访问保留 fail-closed 或 panic 行为，避免静默进入未支持的多核路径。

## 3. 调度、IRQ 与 TLB 准备

- [x] 3.1 为 scheduler 状态访问补充显式所有权和保护边界，不启用跨 CPU run queue、迁移或负载均衡。
- [x] 3.2 保持 i8259/PIT/键盘/exception/syscall 默认路由稳定，并记录 LAPIC、IOAPIC、per-CPU timer、IPI 的后续依赖。
- [x] 3.3 引入 TLB invalidation 边界，使单核路径退化为本地 invalidation，并为未来 target CPU set 与 completion ordering 预留输入。
- [x] 3.4 明确共享状态发布、scheduler 状态、IRQ 可见状态和页表更新的内存序规则。

## 4. 文档与约束记录

- [x] 4.1 更新相关设计文档，说明 SMP 准备边界、单核降级语义、非目标和后续真实 SMP 依赖。
- [x] 4.2 若修改 `docs/en` 或 `docs/zh`，同步更新对应语言镜像并保持相同相对路径。
- [x] 4.3 记录不会改变的地址、ABI、interrupt vector、page-table、disk layout 和默认 userland 行为。

## 5. 验证

- [x] 5.1 运行 `xmake` 或等价的窄 GCC cross-toolchain build；若 `x86_64-elf-gcc`、`x86_64-elf-g++` 或 xmake 不可用，记录阻塞原因与剩余风险。
- [x] 5.2 对新增或修改的 C++ 源/头执行贴近 freestanding C++17、x86_64、no exceptions、no RTTI 的 clang 辅助检查；若不可用，记录工具差距与风险。
- [x] 5.3 对新增或修改的 C++ 源/头执行 clangd 辅助诊断，区分历史诊断、当前 change 引入的诊断和 freestanding 配置误报。
- [x] 5.4 运行受影响路径的最窄单核 QEMU headless smoke；若 QEMU、Bochs、ROM、disk image 或本地配置不可用，记录跳过原因、替代检查和剩余风险。
- [x] 5.5 确认没有启用真实 AP 执行、跨核调度、APIC 默认依赖或用户可见 ABI 变化。
