## Context

BigOS 当前最小可用系统已经依赖一条稳定的单核运行路径：x86_64 IDT/ISR 入口接收异常和外部 IRQ，i8259/PIT 提供外部中断和周期 tick，timer 层维护单调 tick，scheduler 层在 cooperative、blocking、sleep 和 IRQ-return preemption 边界上做调度决策，低层 context switch assembly 保存和恢复 kernel thread 上下文。

这些路径的行为已经可用，但边界仍容易被后续功能扩展侵蚀：core scheduler 可能直接依赖 x86_64 interrupt frame，IRQ 层可能承担 timer/scheduler policy，timer hook 可能扩大为硬件抽象，或者 context-switch 汇编 ABI 在缺少显式记录的情况下被修改。本设计把这些机制按所有权拆分，并要求实现、文档和验证都围绕真实消费点收敛。

本 change 不改变 boot 地址、linker layout、IDT/syscall vector、page-table layout、disk layout、用户态 syscall ABI、`InterruptFrame` 语义或 context-switch frame layout。若实现发现必须改变这些低层假设，应拆分为独立 change。

## Goals / Non-Goals

**Goals:**

- 建立 interrupt/timer/context/scheduler-facing architecture boundary 的实现与规格契约。
- 让 architecture-owned entry/exit 机制、driver-owned hardware programming、timer-owned tick policy、scheduler-owned run queue/preemption policy 之间的依赖方向可审计。
- 保持当前单核 x86_64 Legacy BIOS runnable backend 和现有行为验证路径。
- 为后续 VM/user-entry boundary cleanup 和 backend expansion spike 降低低层耦合风险。

**Non-Goals:**

- 不实现 SMP、per-CPU scheduler、IPI、TLB shootdown 或跨 CPU interrupt routing。
- 不引入 APIC/IOAPIC、HPET、新 timer backend、UEFI runtime parity 或第二 ISA runtime parity。
- 不改变 syscall ABI、用户态 ABI、进程模型、POSIX 兼容范围、动态链接或完整 terminal/job-control 语义。
- 不把当前清理抽象为通用 HAL；只在已有真实消费点建立窄边界。

## Decisions

### Decision: 按所有权分层 interrupt 到 scheduler 的控制流

控制流保持为：architecture ISR stub 保存 CPU/寄存器 frame，进入 interrupt dispatch；dispatch 区分 CPU exception、external IRQ 和 syscall；external IRQ 运行注册 handler 后统一发送 EOI；PIT IRQ handler 只通过 timer-owned API 推进 tick，再通过 bounded scheduler hook 记录调度意图；scheduler 只在明确安全边界执行切换；context switch assembly 只消费 scheduler 准备好的 kernel context。

理由：这保留了现有 vector、EOI 和 frame 语义，同时让 core policy 不需要理解硬件入口细节。

替代方案：把 timer preemption 直接写入 IRQ dispatch 或 ISR assembly。该方案路径更短，但会把 scheduler policy 固化到 architecture entry/exit 中，增加后续维护和验证风险。

### Decision: 保留现有 frame/ABI，先做边界硬化

实现应默认保留 `InterruptFrame`、ISR register-save order、context-switch stack frame、syscall vector 和 i8259 EOI 规则。边界整理可以增加窄 helper、注释、review checklist、source-level checks 和文档，但不应把 ABI 重排作为默认手段。

理由：当前最小可用系统已经依赖这些低层布局；重排会扩大 bootability 和 runtime regression 风险，并模糊本阶段目标。

替代方案：在本阶段同时重构 ISR frame 或 context-switch frame。该方案可能更彻底，但需要独立 ABI migration、emulator validation 和更强回滚计划，不适合作为边界清理默认范围。

### Decision: scheduler 只消费语义化的 architecture/context 边界

scheduler-facing API 应描述“当前上下文是否可抢占”“是否在 IRQ-return safe boundary”“如何切换到下一个 kernel context”等核心语义，而不是暴露 x86_64 descriptor、raw register offset 或 ISR stack layout。x86_64 细节可以保留在低层实现内，但核心调度策略不得新增直接依赖。

理由：本变更是面向真实消费点的 architecture decoupling discipline，不是 speculative backend abstraction；语义接口比通用 HAL 更小，也更符合当前单 backend 成熟度。

替代方案：设计完整 cross-architecture interrupt/context abstraction。该方案过早，需要第二 backend 才能验证抽象质量，容易引入未消费复杂度。

### Decision: 新增极小 architecture-context header

本阶段新增一个独立且极小的 architecture-context header，用于承载 scheduler 和 interrupt-return path 需要消费的上下文语义，例如当前上下文分类、是否允许 IRQ-return preemption、如何进入 architecture-owned context switch boundary 等。该 header 的接口必须描述 core 所需语义，不能泄漏 x86_64 descriptor、裸寄存器 offset、PIC/PIT port IO 或 ISR stack layout。

理由：本变更的核心目标是让 interrupt/timer/context/scheduler-facing 边界成为真实接口，而不是仅靠注释约束。独立 header 可以让调用点、所有权和 review surface 更集中，同时仍保持足够小，避免演化成 speculative HAL。

替代方案：只通过现有 header 的命名和注释收敛边界。该方案改动更小，但容易让 scheduler-facing context 语义继续散落在 IRQ、timer 和 sched 头文件中，后续 review 难以判断哪些接口是 core contract，哪些只是 x86_64 implementation detail。

### Decision: 验证按触及范围分层

文档或 OpenSpec-only 变更使用 OpenSpec status/validate 和 targeted consistency search。C++/assembly/header runtime 变更必须执行最窄可用 cross-toolchain build；涉及 IRQ、PIT、PIC、context switch 或 preemption 行为时，自动化和 smoke 测试首先考虑 QEMU headless。Bochs 定位为早期手工测试和高风险硬件行为交叉验证工具，不作为本变更所有任务的默认硬门槛。

理由：低层行为依赖本地 toolchain、ROM、emulator 和 disk image，验证记录必须区分已执行检查、不可执行原因和剩余风险。

替代方案：每次都强制 QEMU+Bochs 全量验证。该方案覆盖更强，但对文档/spec-only 或纯边界命名整理成本过高，不符合分层验证原则。

### Decision: EOI ordering 先 review/search，必要时补窄脚本

EOI ordering 的默认验证策略是 targeted consistency search、source-level review checklist 和 runtime smoke 组合；只有当实现实际改动 interrupt dispatch、timer IRQ handler、scheduler preemption hook 或 IRQ-return path，并且现有检查无法稳定覆盖“EOI 只由 dispatch owner 发送一次”时，才新增专门静态检查脚本。

理由：EOI ordering 是本变更的核心不变量，但复杂脚本过早引入会增加维护成本。先通过明确 owner、targeted search 和 review notes 固化边界，可以覆盖大多数边界整理；一旦 runtime control flow 发生变化，再用窄脚本补齐可重复检查。

替代方案：立即新增通用静态检查脚本。该方案看似更自动化，但容易误判间接调用和汇编/C++ 混合路径，也可能在接口尚未稳定前固定错误模式。

## Risks / Trade-offs

- [Risk] 只整理边界可能不能立即降低所有 x86_64 耦合。→ Mitigation：任务要求先盘点真实调用点，并把无法安全迁移的耦合记录为后续风险。
- [Risk] 新 architecture-context header 可能伪装成完整多架构抽象。→ Mitigation：header 保持极小，只描述当前 core 所需上下文语义；文档和 specs 明确当前 runnable backend 仍是 x86_64，禁止声明 runtime parity。
- [Risk] IRQ-return preemption 与 EOI ordering 的微小改动可能破坏 timer/scheduler。→ Mitigation：保留单一 EOI 边界，增加 source-level review 和 emulator smoke 任务。
- [Risk] clang/clangd 对 freestanding C++/assembly 边界产生误报。→ Mitigation：把 clang/clangd 作为辅助诊断，验证记录区分当前变更新诊断、历史诊断和 freestanding 配置 false positive。
- [Risk] 本地缺少 `x86_64-elf-gcc`、QEMU 或 disk/serial 配置导致自动化 smoke 不可用。→ Mitigation：任务要求优先记录 QEMU headless 缺失依赖、已通过替代检查和剩余 runtime 风险；Bochs 缺失只影响手工/交叉验证结论。

## Migration Plan

1. 盘点 IRQ、timer、scheduler 和 context switch 的现有调用点，标记 architecture-owned、driver-owned、timer-owned 与 scheduler-owned 边界。
2. 新增极小 architecture-context header，并在不改变低层 ABI 的前提下收敛接口、命名、注释和最小 helper，使 core policy 只消费语义化边界。
3. 更新 OpenSpec 和必要的架构文档；若涉及 `docs/en`，同步 `docs/zh` 对应路径。
4. 执行分层验证；runtime 行为变更优先使用 cross-toolchain build 和 QEMU headless smoke，必要时补充 Bochs 手工/交叉验证或记录不可用原因。
5. 若变更导致 boot、interrupt vector、EOI、frame layout 或 syscall ABI 必须改变，停止本 change 的实现路径并拆分独立 proposal。

Rollback 策略：边界 helper 或文档整理可按子系统回退；任何 runtime 行为整理必须保持旧 ABI 可恢复，并在验证失败时优先回退到现有 dispatch、timer hook 或 context switch 路径。

## Resolved Questions

- 本阶段新增独立且极小的 architecture-context header，但不把它包装成完整 HAL 或多架构 runtime parity 承诺。
- timer preemption 的 EOI ordering 先通过 targeted consistency search、source-level review checklist 和 runtime smoke 覆盖；只有 runtime control flow 变更暴露稳定盲区时，才新增专门静态检查脚本。
- 自动化和 smoke 测试优先使用 QEMU headless；Bochs 更多作为早期手工测试和高风险硬件行为交叉验证工具。
