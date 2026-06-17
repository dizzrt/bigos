## Context

BigOS 当前已经具备进程级 VMA 集合、受限匿名映射、lazy anonymous materialization、`fork`/COW、file-backed read mapping、safe address-space teardown 和 bounded userland。匿名映射的创建路径已经存在，但生命周期还不完整：映射一旦建立，只能随进程 exit/exec teardown 被动释放，用户程序无法主动释放不再使用的匿名区间，也无法在映射创建后按有界规则收紧或改变权限。

本 change 位于当前单核 x86_64 Legacy BIOS runnable baseline 上，不改变 boot handoff、linker 地址、syscall vector、interrupt gate、CR3 切换模型、higher-half kernel、direct map、`KVMEM_BASE` 或 recursive self-mapping。UEFI backend 仍视作非 runtime-parity spike；SMP TLB shootdown 仍不启用，但新增 TLB invalidation 调用点必须表达在既有 SMP 准备边界上。

## Goals / Non-Goals

**Goals:**

- 为匿名 VMA 提供完整有界生命周期：map、unmap、protection change。
- 支持完整 VMA 解除映射，以及对匿名 VMA 的前缀、后缀和中间区间解除映射。
- 支持匿名 VMA 权限变更，并在必要时拆分 VMA、更新 PTE 权限、拒绝 W+X 与不兼容 backing。
- 让 unmap 与 protection change 同步维护 VMA materialization accounting、COW frame 引用计数、页表 ownership、empty page-table reclamation 和当前单核 TLB invalidation。
- 扩展 syscall ABI 与 user libc wrapper，使用户态小程序可以可观察地执行 map/unmap/protect 行为。
- 增加默认关闭 smoke 和源码级检查，覆盖成功路径、拒绝路径、回滚和 fault 行为。

**Non-Goals:**

- 不实现完整 POSIX `mmap`/`munmap`/`mprotect`。
- 不支持 `MAP_FIXED` 覆盖、用户地址 hint、shared writable mapping、file-backed writable mapping、write-back mapping、swap 或 async I/O。
- 不升级 file-backed read-only VMA 为 writable，不允许通过 protection change 绕过 file-backed 只读约束。
- 不启用 SMP、IPI 或 cross-CPU TLB shootdown；只通过既有 invalidation 抽象表达未来边界。
- 不引入动态链接、完整 POSIX libc、job control 或 terminal process groups。

## Decisions

### Decision: 生命周期 API 只作用于 VMA 完整覆盖的页对齐范围

`munmap`-like 和 `mprotect`-like syscall 要求地址与长度页对齐、长度非零、范围不溢出并位于用户低半区。范围必须由现有 VMA 完整覆盖；跨多个 VMA 仅在所有 VMA 均为兼容匿名 backing 且操作可逐段原子化回滚时允许，否则返回确定性负错误。

替代方案是接受任意字节粒度或 POSIX-like 宽松范围。该方案会把边界条件扩散到页表、VMA 拆分和 user-copy 路径，不符合当前 bounded ABI。

### Decision: VMA 拆分先 staging，commit 后再改页表

unmap/protection change 先计算目标范围会产生的 VMA 结果，并预留所需 VMA slot；只有 staging 成功后才修改 VMA 集合和页表。对于中间区间 unmap 或 mprotect，旧 VMA 最多拆成 left/target/right 三段；如果容量不足，操作失败且不改变旧 VMA、PTE 或 materialization accounting。

替代方案是边改 VMA 边 unmap。该方案在容量耗尽或页表释放失败时难以恢复一致性。

### Decision: unmap 释放 materialized 页并保留未物化区间为纯元数据删除

unmap 删除范围内的已物化匿名页必须清 PTE，并根据页状态释放 owned frame 或递减 COW/shared frame 引用计数；未物化 lazy 区间只更新 VMA/materialization accounting，不触发物理页操作。删除 leaf PTE 后沿用现有动态页表 ownership 回收空 PT/PD/PDPT 页。

替代方案是把 unmap 只标记为不可访问，等 teardown 统一释放。该方案不能给用户程序提供可观察的资源生命周期，也会隐藏引用计数错误。

### Decision: protection change 只允许受支持权限收紧或匿名私有权限调整

`mprotect`-like 路径只支持当前 VMA 模型能表达的用户读/写/执行组合，并拒绝 W+X。对 anonymous/private VMA，权限变更会更新 VMA 权限，并对已 present PTE 应用不宽于 VMA 的页表权限。对 COW 页，权限变化不得错误恢复 writable；若 VMA 不再允许写，COW marker 可以保留为无害元数据或被规范化清理，但后续 fault 必须按新权限判定。

替代方案是允许 file-backed VMA 通过 protection change 提升为 writable。该方案会绕过当前 file-backed read-only contract，需要 write-back 和共享语义，超出本 change。

### Decision: syscall 语义采用确定性负错误，不发送 signal

非法范围、权限不支持、VMA 不完整覆盖、capacity exhaustion、页表操作失败或上下文不允许分配时，syscall 返回确定性负错误；只有随后用户实际访问已解除映射或已降权的页面时，才经既有用户 fault-to-lifecycle 路径终止进程。该策略符合当前 bounded syscall 和 signals 基线。

替代方案是立即发送 POSIX-like signal。当前信号能力仍是有界子集，不应把匿名映射生命周期绑定到完整 POSIX signal 语义。

### Decision: TLB invalidation 使用现有准备边界

每次清除 present PTE 或收紧 present PTE 权限后，当前单核路径必须对受影响页执行当前 CPU invalidation；接口命名和调用点应保持可替换为后续 cross-CPU shootdown。当前实现不新增 IPI 或 APIC 行为。

替代方案是依赖 CR3 reload 或忽略 invalidation。该方案会掩盖权限变化和 unmap 后访问的可观察错误。

## Risks / Trade-offs

- VMA 拆分可能耗尽 bounded slot -> 先 staging 并在任务中覆盖容量耗尽，失败时保持旧布局不变。
- unmap 与 COW 引用计数交叉容易 double-free -> 统一走 frame release helper，验证父/子不同退出顺序和 unmap 顺序。
- protection change 与 demand paging 权限可能不一致 -> VMA 权限作为策略源，fault materialization 和 present PTE 权限都不得宽于 VMA。
- 当前没有 SMP shootdown -> 通过现有 invalidation 边界表达单核行为，并在设计中明确不启用 cross-CPU。
- user wrapper 容易暗示完整 POSIX -> 命名和文档保持 bounded 语义，拒绝 unsupported flags 和非页对齐范围。

## Migration Plan

- 先实现 VMA range operation helper：覆盖页对齐校验、完整覆盖检查、拆分 staging、slot 预留和 rollback。
- 接入匿名 unmap：删除 VMA 区间，释放 materialized 页、COW/shared frame 引用和空页表页。
- 接入匿名 protection change：更新 VMA 权限并同步 present PTE 权限，拒绝 unsupported backing 与 W+X。
- 扩展 syscall number、内核 dispatch 和 user wrapper，并增加最小用户程序或 smoke 消费者。
- 增加源码级检查、xmake build、OpenSpec strict validation 和 QEMU headless marker smoke。
- 若实现中发现风险不可控，保留 helper 但不暴露 syscall，并在 validation notes 记录未启用表面和剩余风险。

## Open Questions

- 暂无；本 change 固定为 bounded anonymous lifecycle，不尝试定义完整 POSIX 兼容层。
