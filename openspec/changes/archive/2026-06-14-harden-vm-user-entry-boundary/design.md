## Context

BigOS 当前 baseline 已经从单纯进入 ring3 扩展到默认 PID-1 init、`/bin/sh`、bounded `/bin/*`、`execve`、demand paging、bounded `fork`/COW、signals、time/identity、fd/VFS 和 userland runtime。运行路径已经可用，但 VM 与用户态入口相关的职责跨越 `kernel/mm`、`kernel/core/proc`、`kernel/core/irq`、`kernel/core/syscall` 和 x86_64 低层实现，后续扩展容易把核心策略和架构机制再次混在一起。

本设计将边界聚焦在真实消费点：VMA 与页表 materialization 表达 core VM policy；地址空间 activation 表达切换到哪个根和何时切换；user-entry 表达进入或返回用户态的语义；fault dispatch 表达 user fault recovery 与 kernel diagnostic/panic 的分流。x86_64 的 CR3、GDT/TSS、`iretq`、page-table bit、interrupt frame 和 assembly stub 细节仍保留在架构实现或明确命名的低层路径中。

本 change 不改变 boot 地址、linker layout、higher-half mapping、direct map、KVMEM、recursive/self-mapping、syscall vector、IDT vector、disk layout、用户栈布局或现有 CR3 切换语义。若实现发现必须改变这些低层 ABI，应停止并拆分独立 change。

## Goals / Non-Goals

**Goals:**

- 建立 VM policy、address-space activation、user-entry mechanics 和 fault handling 的核心/架构边界契约。
- 让核心层通过语义接口消费“用户地址空间”“进入用户态”“从用户 fault 恢复或终止”等概念，而不是散落消费 x86_64 私有寄存器、frame 或 descriptor 细节。
- 保持当前 x86_64 Legacy BIOS/MBR/exFAT runnable backend 和已有 bounded userland 行为。
- 为后续 backend expansion、terminal preparation、SMP preparation 或更宽用户内存能力提供更清晰的依赖面。

**Non-Goals:**

- 不实现 broad file-backed `mmap`、dynamic linking、shared libraries、完整 POSIX process/job-control model、完整 POSIX libc 或 async I/O。
- 不实现 UEFI backend、第二 ISA runtime parity、SMP、TLB shootdown、APIC/IOAPIC、HPET 或新 storage/device backend。
- 不改变现有 boot/linker/page-table/self-mapping/direct-map/syscall vector/user ABI/disk layout 假设。
- 不把本阶段边界包装成完整 HAL；只在当前已有真实消费点建立窄契约。

## Decisions

### Decision: VM policy 与 page-table materialization 分层

核心 VM policy 由 VMA、进程地址空间元数据和 user-buffer validation 表达；page-table materialization 只负责把已授权的 VMA 结果转换为当前可执行的映射状态。页表 helper 可以暴露 map/unmap/probe/teardown 等语义，但不得让普通核心策略直接依赖 x86_64 PTE bit 编码或 recursive mapping 细节。

理由：现有 demand-zero、COW、stack growth、`brk` 和 `execve` 已经依赖 VMA 与页表同时正确。明确分层能避免后续 fault recovery 或 syscall user-copy 只看 present PTE 而绕过 VMA 策略。

替代方案：把 VMA 策略和页表探测合并成一个大 VM API。该方案可能简化调用，但会隐藏 materialized state 与 policy state 的差异，增加 rollback、COW 和 fault handling 风险。

### Decision: 地址空间 activation 是独立边界

地址空间派生、用户页映射、CR3/address-space activation、active-root 恢复和 safe teardown 必须作为不同阶段处理。核心进程逻辑可以请求“激活某个用户地址空间”或“恢复安全内核上下文”，但 x86_64 CR3 写入、TLB 刷新和低层寄存器约束应留在 architecture-owned 实现中。

理由：`execve`、`fork`/COW、user fault 终止和 reaper 都可能触及地址空间生命周期。将 activation 独立出来，可以避免在 unsafe return path 上释放仍被当前 CR3 或当前栈依赖的资源。

替代方案：继续让 proc、fault handler 和 user-entry 路径直接写入或假设 CR3 状态。该方案短期改动少，但会扩大低层状态耦合，使 future backend 或 SMP preparation 难以审查。

### Decision: 用户态入口只暴露核心语义

user-entry 边界应表达“准备进入用户态”“从 syscall/exception 返回用户态”“终止当前用户进程并切到安全内核路径”等语义。x86_64 的 `iretq` frame、GDT/TSS/RSP0、segment selector、register restore order 和 assembly entry/exit 细节不得成为普通核心子系统的公开契约。

理由：当前 ring3 入口和 syscall/fault 路径已经可用，边界硬化的目标是保留行为同时降低扩展风险，而不是重排 ABI。

替代方案：在本阶段重写 user-mode entry assembly 和 frame layout。该方案可能带来更清晰的低层实现，但风险过高，应作为独立 ABI change 设计和验证。

### Decision: fault handling 明确 user recovery 与 kernel diagnostic 分流

fault dispatch 必须保留 CPL/user-vs-kernel 分类。用户态 `#PF` 可在已实现且 VMA 授权的 demand-zero、COW、stack-growth 等有界路径中恢复；不可恢复的用户 fault 进入进程终止和 safe reaper；kernel fault 仍进入诊断或 panic 边界，不被误当作可恢复用户 fault。

理由：错误地恢复 kernel fault 或在 fault path 中释放活动资源会破坏内核安全性和可调试性。保持分流能延续现有 bounded userland 成熟度。

替代方案：扩大 fault handler 为通用 pager。该方案会引入 broad file-backed `mmap`、async I/O 或更完整 VM subsystem 的需求，不属于本阶段范围。

### Decision: 新增独立的 architecture VM/user-entry boundary header

实现阶段应新增一个独立且极小的 architecture VM/user-entry boundary header，用于承载核心层需要消费的地址空间激活、用户态进入/返回、用户 fault 分类和架构 VM 机制边界语义。该 header 只暴露核心需要的语义类型与 helper，不暴露 x86_64 CR3 编码、GDT/TSS 布局、raw interrupt-frame offsets、`iretq` frame 细节或 PTE bit 编码。

理由：VM/user-entry/fault 边界跨越 `kernel/mm`、`kernel/core/proc`、`kernel/core/irq` 和低层 x86_64 entry code；独立 header 能让所有权、依赖方向和 review surface 更集中，同时避免把语义边界继续散落在既有头文件中。

替代方案：复用现有头文件并收窄可见性。该方案改动更小，但容易让 VM/user-entry 语义继续和历史实现细节混杂，后续 review 难以判断哪些接口是 core contract，哪些只是 x86_64 implementation detail。

### Decision: 验证按触及范围分层

OpenSpec-only 或文档-only 工作使用 OpenSpec status/validate 和 targeted consistency search。若实现触及 C++/assembly/header runtime path，必须运行最窄可用 cross-toolchain build；涉及 user-entry、CR3、page fault、demand paging、COW 或 syscall return path 时，环境支持下优先使用 QEMU headless 对应 smoke，并记录无法执行的 QEMU/Bochs/toolchain 原因。

理由：本阶段跨越 VM、IRQ、proc 和 syscall 低层路径，但不是所有任务都会改变 runtime control flow。分层验证可以降低无效成本，同时保留 bootability 和 userland 行为风险记录。

替代方案：每个子任务都强制 emulator 全量验证。该方案覆盖更强，但对 spec/docs 和纯边界命名整理成本过高，也更容易受本地环境依赖阻塞。

## Risks / Trade-offs

- [Risk] 边界整理被误解为完整多架构 HAL。→ Mitigation：spec 和文档明确当前 runnable backend 仍是 x86_64，接口只描述当前核心真实消费的语义。
- [Risk] activation 与 teardown 分层不足导致释放 active CR3、kernel stack 或 borrowed high-half mapping。→ Mitigation：任务要求先盘点当前 user root、VMA、page-table ownership 和 reaper 路径，再做最小迁移。
- [Risk] fault handling 清理意外扩大可恢复范围。→ Mitigation：spec 固定用户 fault recovery 只覆盖既有 VMA-backed bounded case，kernel fault 保持 diagnostic/panic。
- [Risk] 低层 ABI 被顺手重排。→ Mitigation：proposal、design 和 specs 均要求保持 boot/linker/page-table/vector/user ABI 假设，任何重排拆成独立 change。
- [Risk] 本地缺少 cross-toolchain、QEMU、Bochs 或 disk/serial 配置。→ Mitigation：validation task 必须记录缺失依赖、替代检查和剩余 runtime 风险。

## Migration Plan

1. 盘点 VM/user-entry/fault 调用点，标记 core-owned policy、architecture-owned mechanism 和 safe teardown 边界。
2. 在不改变现有低层 ABI 的前提下，新增独立且极小的 architecture VM/user-entry boundary header，让核心层消费 VM/user-entry/fault 概念而非裸 x86_64 细节。
3. 更新 OpenSpec delta 和必要架构文档；若更新 `docs/en`，同步 `docs/zh` 对应路径。
4. 按触及范围执行 OpenSpec、targeted search、build 和 QEMU/Bochs 验证，并记录不可用环境原因。
5. 若发现必须改变地址布局、CR3 semantics、entry frame、syscall vector 或 fault ABI，停止本 change 实现并拆分独立 proposal。

Rollback 策略：边界 helper、headers 和文档可以按子系统回退；runtime 路径改动必须保持旧入口、旧 fault dispatch 和旧 address-space activation 可恢复，验证失败时优先回退到现有稳定路径。

## Resolved Questions

- 实现阶段新增独立且极小的 architecture VM/user-entry boundary header，不复用既有头文件承载该核心语义边界。
- 现有 source-level checks 是否足以覆盖 CR3/user-entry/fault 分层，还是需要新增窄脚本检查核心层对 x86_64 私有细节的直接依赖？需要在实现阶段根据实际改动决定。
