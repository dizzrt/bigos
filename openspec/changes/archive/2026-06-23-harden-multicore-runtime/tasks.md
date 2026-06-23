## 1. Scope And Invariant Audit

- [x] 1.1 审计 AP startup、per-CPU timer、scheduler、IPI、TLB shootdown、`mm context` residency 的共享状态所有权，列出需要保护的 CPU-local、全局和 IRQ-safe 状态。
- [x] 1.2 定义并记录 scheduler domain lock、IPI request state、shootdown completion state、CPU topology state、`mm context` residency 之间的锁顺序和等待前释放规则。
- [x] 1.3 审计 timer/IPI/shootdown IRQ handler，确认其不进行普通动态分配、文件系统/块 I/O、user-copy 或 scheduler-managed blocking wait。
- [x] 1.4 审计所有跨 CPU target selection，确保 offline、failed、unsupported、non-schedulable CPU 被拒绝或显式排除。

## 2. Scheduler Hardening

- [x] 2.1 加固 remote enqueue 和 wait/timeout wakeup 路径，确保线程进入 runnable 时最多属于一个 CPU run queue。
- [x] 2.2 加固 AP timer tick 与 IRQ-return scheduling，确保 AP 只通过本 CPU scheduler domain 进行 accounting、preemption 和 next-thread selection。
- [x] 2.3 增加 scheduler 诊断字段，覆盖 current CPU、target CPU、thread state、queue membership 和触发操作。
- [x] 2.4 增加或更新 bounded scheduler stress 入口，覆盖远程唤醒、timeout wakeup、idle CPU 被 nudged 后运行本地 work 的路径。

## 3. IPI And TLB Shootdown Hardening

- [x] 3.1 加固 typed IPI delivery，确保 scheduler-nudge 与 TLB-shootdown 的 vector 分类、ack state 和 completion 语义相互独立。
- [x] 3.2 加固 TLB shootdown 发布顺序，确保页表更新先于 IPI 投递可见，且 required target ack 完成前不释放 frame、不复用地址、不返回依赖该 invalidation 的用户态路径。
- [x] 3.3 增加 shootdown timeout fail-closed 诊断，至少包含 requesting CPU、target set、missing ack、`mm context`、generation/token 和 timeout 类型。
- [x] 3.4 确认 IPI/shootdown failure path 使用 panic-safe serial/VGA 诊断边界，不绕过现有诊断保护直接访问 VGA。

## 4. AP Startup And Timer Failure Isolation

- [x] 4.1 加固 AP startup timeout 处理，确保未按时上线的 AP 保持 failed/offline，并不进入 scheduler、IPI 或 shootdown target set。
- [x] 4.2 加固 AP per-CPU timer 初始化状态检查，确保初始化前 tick 被拒绝或进入确定性诊断路径。
- [x] 4.3 加固 LAPIC/IOAPIC/per-CPU timer fallback 诊断，明确 fail-closed 或 BSP-only fallback，不声称多核 APIC-backed runtime coverage。

## 5. Validation And Tooling

- [x] 5.1 增加默认关闭的 multi-core hardening build switch 与 smoke 入口，覆盖 scheduler stress、IPI delivery、TLB shootdown completion 和 baseline userland 回归。
- [x] 5.2 更新或新增 helper 脚本支持 QEMU headless 多核 smoke；若修改 Python 文件，使用 `uv run ruff check`、`uv run ruff format --check`、`uv run pyright`、`uv run pytest` 验证或记录 blocker。
- [x] 5.3 运行 `xmake` 或等价 x86_64 cross-toolchain 构建，修复当前变更引入的构建错误。
- [x] 5.4 对新增/修改的 C++ 源码和头文件运行尽可能贴近 freestanding C++17/x86_64 cross-build 的 clang/clangd 辅助检查，区分历史诊断、当前变更诊断和工具配置缺口。
- [x] 5.5 在 QEMU headless 可用时运行 bounded multi-core hardening smoke，并记录 CPU 数、通过 marker、超时参数和 baseline userland 结果。
- [x] 5.6 在 Bochs 可用且适合 APIC/早期启动交叉验证时运行 targeted smoke；不可用时记录跳过原因和残余 APIC/多核风险。

## 6. Documentation And Closure

- [x] 6.1 更新相关验证记录或文档，明确已覆盖的多核调度、IPI、shootdown、timer、AP startup 场景以及未覆盖风险。
- [x] 6.2 确认文档不把 BigOS 描述为完整 SMP、CPU hotplug、NUMA、完整 POSIX 并发模型、异步 I/O 或 UEFI runtime parity。
- [x] 6.3 运行 OpenSpec 状态/校验命令，确认 change artifacts、spec deltas 和 task checklist 可用于实现阶段。
