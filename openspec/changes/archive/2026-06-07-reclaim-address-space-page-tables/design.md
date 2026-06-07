## Context

阶段 5 已建立显式页属性、root-targeted user mapping、用户页表根派生和 CR3 activation helper；阶段 6 已在默认关闭的 `user_program_smoke` 下创建最小 `Process`，映射 flat embedded user image、用户数据页和用户栈，并通过 TSS/RSP0 + `iretq` 进入 ring3，最后用 `SYS_WRITE`/`SYS_EXIT` 闭环。当前退出路径只记录 terminated/fault 状态，不承诺回收用户地址空间、用户物理页、动态页表页或 process kernel stack。

本 change 跨越 `src/mm` 和 `src/kernel/proc`：一侧需要让页表 map/unmap 能识别哪些页表页是运行时动态创建且可回收，另一侧需要在进程不再运行、且不处于当前用户 CR3/当前内核栈的路径上执行 teardown。设计必须保持单核 freestanding C++17、无 SMP、无 hosted libc、无异常/RTTI，并且不移动 boot 固定地址、higher-half base、`KVMEM_BASE`、direct map、recursive self-mapping 或 BootInfo handoff ABI。

## Goals / Non-Goals

**Goals:**

- 为运行时动态创建的页表页建立 owner、level 和 present-entry 计数或等价可验证元数据。
- 在叶子 PTE 清除后向上检查空 PT/PD/PDPT，回收只由当前 owner 动态创建且不再包含 present entry 的页表页。
- 为用户地址空间提供 teardown helper，释放用户 leaf page、用户 PML4 root、动态页表页和已退出进程的 kernel stack。
- 明确 `SYS_EXIT`、用户 `#PF` fault path、scheduler/idle 后续清理路径之间的职责，避免自毁当前执行栈或在错误 CR3 下释放资源。
- 保持默认 boot 行为、interrupt/syscall EOI 语义、地址布局和公开内存分配 API 语义不变。

**Non-Goals:**

- 不实现 SMP TLB shootdown、per-CPU 页表状态、抢占调度或 sleepable reclamation。
- 不实现 demand paging、COW、`mmap`、`brk`、自动栈增长、用户态 fault 恢复或 signal。
- 不实现多进程公平调度、`wait`、zombie reaping、进程树或 PID namespace。
- 不从文件系统加载用户 ELF，不引入块设备、VFS 或用户态 libc。
- 不回收 boot/kernel/direct-map/KVMEM/self-mapping 依赖页表页，也不改变 static kernel mappings。

## Decisions

### Decision: 页表页 ownership 与 present-entry 计数绑定到 map/unmap primitive

动态页表页由 root-targeted map primitive 创建时登记为 `PageTableOwner` + `PageTableLevel` + present-entry count。owner 至少区分 kernel vmem、user address space、boot/static/unknown 三类；只有明确标记为当前 address space 或 kernel vmem 动态页表页的 page-table frame 才能被 reclaim。map 写入新 present descriptor 时递增父页表页计数，unmap 清除 present descriptor 时递减对应计数。

理由：空页表回收必须知道“这个页表页是否由当前运行时创建”和“它是否真的为空”。只扫描 512 项虽然简单，但缺少 ownership 时容易误回收 boot handoff 或 kernel 高半区共享页表；只依赖计数又需要在 map/unmap/failure rollback 中保持一致，因此计数必须在 primitive 边界维护。

替代方案：teardown 时递归扫描所有页表页并按虚拟地址范围释放。否决理由：扫描可以作为验证或 fallback，但没有 owner 标记仍无法安全区分静态页表页，且更容易误处理 recursive self-mapping、高半区共享条目和 direct-map 条目。

### Decision: 用户地址空间 teardown 只遍历用户低半区 owned 条目

用户地址空间 teardown 从用户 PML4 root 出发，仅处理用户低半区和该 process 明确拥有的 leaf ranges。高半区条目视为 borrowed kernel mappings，不递归释放、不清除、不刷新其 leaf；recursive self-mapping、direct map、KVMEM、kernel image 和 boot handoff 所需页表保持不变。用户 PML4 root 在所有 owned 低半区子页表与用户 leaf page 释放后再归还 buddy。

理由：派生用户 root 复制内核高半区以保证 syscall/exception/IRQ/diagnostic path 可达，这些条目不是进程私有资源。teardown 若遍历全 root，会把共享内核映射纳入回收风险。

替代方案：由 map 记录的 range list 逐段 unmap，不递归页表树。该方案适合作为 leaf page 释放来源，但无法独立证明中间 PT/PD/PDPT 页为空；实现可结合 range list 释放 leaf，再用 owned 页表元数据向上回收。

### Decision: `SYS_EXIT` 和用户 fault 只标记待回收，安全上下文执行 teardown

`SYS_EXIT`、用户态 `#PF` 和非法用户 buffer 处理路径将当前进程状态置为 terminated/faulted，并记录 exit code 或 fault reason。它们不得直接释放当前执行所依赖的 kernel stack、当前 CR3 或 process object。实际 teardown 在已切回 kernel root、当前执行栈不属于被回收 process、且非 IRQ handler 的上下文中执行；阶段 6.5 可使用 bounded reaper helper、scheduler/idle handoff 或 smoke return path。

理由：用户退出时仍可能运行在该进程的内核栈上，且当前 CR3 可能是用户 root。直接 free 会造成 use-after-free 或在 user CR3 下访问未映射诊断/allocator 路径。

替代方案：`SYS_EXIT` handler 内立即 teardown。否决理由：最容易释放当前栈或当前页表 root，和现有 `first-user-program` 对“退出不立即回收当前资源”的要求冲突。

### Decision: TLB invalidation 保持单核当前 CPU 语义

每个被清除的 leaf PTE 和非叶 descriptor 对当前 CPU 执行 `invlpg` 或等价 CR3 reload 边界。由于当前假设是单核、无 SMP、无其它 CPU 使用同一地址空间，本 change 不定义跨 CPU shootdown。释放当前 active root 前必须先切换到 kernel root 或其它安全 root。

理由：当前内核只有单核 cooperative scheduler，现有 unmap 已以当前 CPU TLB invalidation 为边界。引入 shootdown 会扩大到 SMP、IPI 和 per-CPU 状态设计。

替代方案：所有 teardown 后统一 CR3 reload。该方案可作为简单实现，但仍需对 leaf unmap 保持可验证刷新边界；对于非当前 root 的 teardown，记录不需要立即 `invlpg` 但必须保证 root 不再被激活。

### Decision: 失败路径保守停止或保持资源 owned

页表页元数据分配失败时，map primitive 必须回滚本次已写 descriptor/PTE 和计数，或不发布该映射并返回失败。teardown 中遇到不一致 ownership、计数下溢、非 owned 页表页或无法安全释放的对象时，不继续部分回收到不可推理状态；实现可以记录 `BIGOS_` marker、保留资源泄漏并返回失败，或走统一 panic，具体按调用点安全性决定。

理由：内存回收路径的错误比泄漏更危险。阶段 6.5 优先保证 bootability、页表不被误释放和诊断可观测。

替代方案：teardown 忽略异常并尽力释放。否决理由：低层页表错误无法安全忽略，可能导致后续 kernel fault 或 silent memory corruption。

## Risks / Trade-offs

- [Risk] 页表页计数与实际 present entries 不一致导致误回收或泄漏 -> Mitigation: map/unmap/failure rollback 均在同一 primitive 边界更新计数，源码级测试覆盖计数、扫描校验和失败回滚。
- [Risk] 用户 teardown 误遍历内核高半区共享映射 -> Mitigation: teardown 只处理用户低半区和 owned 元数据，源码级检查确认 direct map/KVMEM/self-mapping/kernel image 常量不移动且不被释放。
- [Risk] `SYS_EXIT` 后释放当前内核栈或 active CR3 -> Mitigation: exit/fault 只标记待回收，teardown 要求安全 kernel root、非 IRQ handler、非当前被回收 stack。
- [Risk] 回收页表页时 TLB 未失效导致 stale translation -> Mitigation: leaf unmap 执行当前 CPU invalidation；释放 active root 前先切 CR3；非 active root teardown 记录无需 shootdown的单核前提。
- [Risk] teardown 失败使 smoke 资源仍泄漏 -> Mitigation: 失败路径输出 deterministic marker 或统一 panic；验证记录区分“安全保留资源”与“已成功回收”。
- [Risk] clang/clangd 对 freestanding flags 或 cross target 支持不完整 -> Mitigation: 任务要求把 clang/clangd 作为辅助信号，记录不可用或 false positive，不替代 xmake cross build。

## Migration Plan

1. 扩展页表 map/unmap 内部数据模型，添加动态页表页 ownership/level/count 记录和源码级检查。
2. 将 kernel vmem/free_pages 和 user root-targeted map/unmap 接入 owned 页表页登记，补齐失败 rollback。
3. 实现空 PT/PD/PDPT 向上回收 helper，限定 owner、level、用户低半区/动态 kernel vmem 范围和 TLB invalidation 边界。
4. 为 `Process` 增加待回收状态和 teardown helper，释放用户 leaf page、用户 PML4 root、动态页表页和 process kernel stack。
5. 将 `SYS_EXIT` 与用户 fault 路径改为标记 terminated/faulted + enqueue/reaper handoff，不在当前栈上释放。
6. 补源码级测试、默认/`user_program_smoke` 构建、OpenSpec validation 和可选 Bochs serial marker smoke。

回滚策略：保留新增元数据但关闭空页表页回收调用点，可退回仅清除 leaf PTE 和记录 terminated 的阶段 6 行为；若默认 boot 或用户 smoke 受影响，优先回退 `SYS_EXIT`/fault 的 reaper wiring，保持页表 ownership 只作为诊断信息。

## Open Questions

- 页表页元数据存放在现有 `PageBlock`/buddy metadata、独立 slab 对象，还是用户 address-space 对象内的 bounded table？实现阶段需要按初始化顺序和 failure rollback 复杂度选择。
- kernel vmem 的空页表回收是否与用户地址空间 teardown 同步落地，还是先只覆盖 user-root owned 页表？proposal 已要求支持 free_pages/future user unmap 的边界，任务可按风险拆分实现。
- teardown 成功后的 smoke oracle 使用现有 `BIGOS_USER_EXIT` 扩展状态，还是新增更明确的 `BIGOS_USER_RECLAIMED` marker？实现阶段应选择脚本可判定且不污染默认 boot 的 marker。
