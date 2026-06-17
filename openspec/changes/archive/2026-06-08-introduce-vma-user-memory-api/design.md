## Context

BigOS 当前已经具备用户地址空间派生、owned 页表回收、进程生命周期、bounded ELF64 `exec`、`wait`/`exit`、fd/VFS 壳层和 `int 0x80` syscall 分发。用户内存的策略仍分散在 ELF loader、用户栈布置、页表映射和 syscall user-buffer 校验中，缺少进程级 VMA 模型来回答“某个用户地址范围是否属于进程、具有什么权限、是否允许增长或映射”。

userland runtime baseline 位于 demand paging、COW、`fork` 和用户态 libc 之前。它应只引入用户虚拟内存策略和受控 API，而不是把 `#PF` handler 改造成通用缺页调度器。默认行为仍保持单核、同步、bounded、freestanding；页表地址布局、boot/linker 地址、syscall vector、IRQ/exception EOI 语义和 Legacy BIOS/raw image 路径不变化。

## Goals / Non-Goals

**Goals:**

- 为每个进程维护一个 bounded VMA 集合，记录用户低半区区域、权限、用途、增长策略和拥有关系。
- 将 ELF segment、heap、anonymous mapping、user stack 和 guard 区域都纳入 VMA 策略。
- 提供最小 `brk` 和受限匿名映射 API，支持后续 libc-adjacent startup 的 heap 需求。
- 让 syscall user-buffer 校验基于 VMA 权限和范围，而不是只依赖页表 present 探测。
- 定义用户栈 guard 与最大增长范围；只允许明确的 stack-growth fault 走受控恢复，其他用户缺页仍进入进程 fault/exit 生命周期。
- 明确 VMA 元数据在 exec commit/rollback、exit、fault、reaper 和 address-space teardown 中的释放顺序。

**Non-Goals:**

- 不实现通用 demand paging、COW、`fork`、swap、page cache、file-backed mapping、shared mapping 或完整 POSIX `mmap`/`munmap`/`mprotect`。
- 不引入 SMP、TLB shootdown、可抢占页错误恢复、信号、用户态 libc 或动态链接。
- 不改变 higher-half kernel、direct map、`KVMEM_BASE`、recursive self-mapping、boot handoff、linker script、syscall vector 或中断门权限边界。
- 不要求 writable filesystem、UEFI/OVMF、virtio/AHCI/NVMe 或新磁盘镜像格式。

## Decisions

### Decision: 使用进程拥有的 bounded VMA 表

VMA 采用进程对象拥有的 bounded 表或等价有界容器，条目包含 `[start, end)`、权限、用途、增长方向、最大边界和 backing 类型。这样符合当前研究内核的有界资源模型，能避免树结构、动态重平衡和复杂锁。

替代方案是引入红黑树或通用 interval tree。该方案更接近通用 OS，但需要更多动态分配、迭代器语义和失败路径，不适合当前单核 bounded 阶段。

### Decision: VMA 描述策略，页表描述当前 materialized 映射

VMA 先回答地址范围是否合法、权限是否允许、是否允许增长；页表随后回答当前页是否 present、属性是否与 VMA 一致。`copy_from_user`、`copy_to_user`、syscall path 和 exec/loader 都应先查询 VMA，再执行 bounded page-table 或 safe-copy 操作。

替代方案是继续只做页表探测。该方案无法表达 guard、heap 上限、anonymous range 预留和未 materialized 但策略合法的 stack-growth 区域。

### Decision: `brk` 只管理单一 heap VMA

进程创建或 exec commit 后建立一个可选 heap VMA，`brk` 只能在该 VMA 的 bounded 范围内扩展或收缩。扩展时按当前阶段选择 eager allocation/eager mapping；失败必须回滚到旧 break。收缩时 unmap 被移除的页，并保持页表 ownership 回收语义。

替代方案是让 `brk` 只改元数据、等待 demand paging materialize。该方案会把userland runtime baseline 绑定到通用缺页恢复，不符合路线图中“先策略、后 demand paging”的顺序。

### Decision: 匿名映射暴露为受限最小 syscall，而非完整 mmap

匿名映射第一版同时提供内核 helper 和用户可见的最小 syscall，但 syscall 语义只覆盖 page-aligned、bounded、private、non-file-backed 用户页，权限来自 VMA flags，并拒绝与既有 VMA 重叠。不支持文件 fd、shared mapping、fixed overwrite、caller-provided fixed address 或权限升级到 W+X。

替代方案是直接设计 POSIX-like `mmap`。该方案会牵涉 fd、page cache、文件写回、共享语义和权限模型，超出userland runtime baseline。

### Decision: `brk` 与匿名映射使用内核选择的线性用户窗口

第一版不接受用户传入的地址 hint。`brk` 固定管理 exec 布置出的单一 heap VMA；匿名映射由内核在预留 anonymous mapping 窗口内按简单线性策略选择非重叠范围。这样可以让失败路径、VMA 重叠检查和 smoke oracle 保持确定性。

替代方案是接受 hint 或 fixed address。该方案会提前引入地址选择策略、碰撞覆盖规则和 POSIX-like `mmap` 兼容性问题，留给后续 userland runtime 或 mmap 扩展阶段。

### Decision: 用户栈增长只允许受控 #PF 恢复

默认 `#PF` 语义仍是内核诊断或用户进程 fault 终止。只有当 fault 来自 CPL3、地址落在当前进程 stack-growth VMA 的 guard/增长窗口、访问类型符合栈权限、当前上下文可分配且不会破坏 reaper/IRQ 规则时，内核才可以分配并映射新栈页，然后恢复用户态。

替代方案是所有 VMA miss 都触发 demand paging。该方案需要 fault 重试、OOM kill、IO-backed paging 和并发语义，留到后续 demand paging change。

### Decision: VMA 生命周期跟随进程镜像

ELF exec 准备新镜像时在 staging image 上构建 VMA 集合，commit 前失败释放新 VMA 和新页；commit 后旧镜像通过既有 safe reaper/teardown 释放。exit/fault 不在活动 syscall/exception 路径直接释放 VMA；最终释放发生在安全 reaper 上下文。

替代方案是在 process object 上原地修改 VMA 表。该方案在 exec 失败和旧镜像仍可运行时难以保证 rollback，不符合现有 lifecycle rollback 约束。

### Decision: 新增独立 `user_vma_smoke`

userland runtime baseline 新增独立默认关闭的 `user_vma_smoke` 开关，用于验证 VMA 插入/拒绝、`brk`、匿名映射、VMA-backed user-copy 和 stack-growth 的最小可观察路径。该 smoke MAY 与 `user_elf_smoke`、`syscall_smoke` 组合运行，但不依赖它们作为唯一验证入口。

替代方案是只复用 `user_elf_smoke` 与 `syscall_smoke`。该方案会让 VMA 失败定位依赖更大的用户态闭环，难以区分 exec/syscall/VFS 问题与 VMA 策略问题。

## Risks / Trade-offs

- VMA 表容量过小可能阻塞后续用户程序形态 -> 采用显式 bounded 上限、确定性错误返回，并在验证记录中覆盖容量耗尽。
- eager allocation 让 `brk`/anonymous mapping 更简单但浪费物理页 -> 当前阶段优先确定性和无 demand paging；后续可在 VMA contract 不变的前提下改为 lazy materialization。
- stack-growth 恢复路径可能破坏现有 diagnostic-only `#PF` 假设 -> 只允许 CPL3 stack-growth VMA 命中时恢复，CPL0 fault 和非栈用户 fault 保持现有诊断/终止语义。
- syscall buffer 校验若只检查 VMA 可能漏掉未映射页 -> user-copy helper 必须在 VMA 校验后确认 present/user 权限或安全检测访问失败，并返回 deterministic error 或终止当前进程。
- exec staging 同时维护页表和 VMA 可能增加 rollback 复杂度 -> 将 staging image 资源集中记录，commit 前失败只释放 staging，commit 后失败走 process fault/reaper。

## Migration Plan

- 先新增 VMA 数据结构和纯源码级单元/静态检查，不改变正常 boot 路径。
- 将 exec ELF segment、user stack 和 heap 初始边界写入 staging VMA 集合，再接入 process object。
- 将 user-buffer 校验 helper 改为 VMA-first，同时保留页表 present/user 检查。
- 增加 `brk` 和受限匿名映射 API，再按 syscall number 暴露给 ring3 消费者。
- 增加 stack-growth fault 的严格 gate；若 gate 未命中，沿用用户 fault 终止。
- 若实现失败或验证不可用，回滚策略是禁用新增 syscall/API 调用点，保留 VMA 数据结构不参与正常 boot，并记录未验证风险。

## Open Questions

- 暂无；第一版 anonymous mapping syscall、线性用户窗口和独立 `user_vma_smoke` 已作为userland runtime baseline 决策固定。
