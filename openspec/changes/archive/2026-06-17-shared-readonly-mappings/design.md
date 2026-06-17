## Context

BigOS 当前已具备 VMA-backed user memory、anonymous demand paging、fork/COW、file-backed read mapping、page/buffer cache 和单核 TLB invalidation boundary。file-backed read mapping 已能把只读文件范围登记为 lazy VMA，并在 CPL3 read fault 时按需装入只读页；fork 路径也已存在引用计数意识，避免把已物化的只读 file-backed 页简单深拷贝。

缺口是“跨进程、同 backing、同页偏移”的只读文件页仍缺少明确的共享页目录与生命周期契约。后续更成熟的用户程序会重复映射相同 text/data 文件页，如果每个进程都独立装入，将浪费物理页并扩大 teardown/fork 的复杂度。该设计把只读 file-backed 页抽象为可引用计数的共享物化页，同时保持当前 x86_64 单核、同步 I/O、Legacy BIOS 默认启动路径不变。

## Goals / Non-Goals

**Goals:**

- 支持多个进程共享同一只读 file-backed 页，复用条件由 backing file identity、页对齐文件偏移和只读权限边界决定。
- 让 exec 装载的只读 ELF text/rodata 页也进入同一共享目录，避免多个进程运行同一静态程序时重复物化相同只读页。
- 让 page fault、fork、exec、unmap、mprotect-style protection change、exit/reaper teardown 都通过同一引用计数规则处理共享只读页。
- 保证共享只读页永远以 read-only user PTE 发布；写访问必须走 deterministic user fault，不能进入 COW 或写回路径。
- 通过现有 TLB invalidation boundary 表达 PTE 发布、权限收紧和 unmap 后的可见性；当前单核实现仍退化为 local invalidation。
- 增加默认关闭的行为验证，并记录 OpenSpec、构建、源码级检查和可用时的 QEMU headless smoke。

**Non-Goals:**

- 不实现 writable shared mapping、file-backed write-back mapping、完整 POSIX `MAP_SHARED`、shared anonymous mapping 或 swap。
- 不实现动态链接、共享库装载、ELF text 自动跨 exec 全局去重之外的完整 loader 策略。
- 不启用 SMP、AP startup、APIC/IPI、跨 CPU 调度或真正的 cross-CPU TLB shootdown。
- 不改变 syscall vector、既有 syscall 编号、寄存器 ABI、boot/linker 地址、direct map、recursive self-mapping、磁盘布局或默认启动 backend。

## Decisions

1. 共享单位选择为“只读 file-backed 物化页”，不是 VMA。

   共享表以 `(backing object identity, page-aligned file offset, immutable read-only mapping attributes)` 为键，值为物理 frame、有效内容长度、引用计数和状态。VMA 仍是每个进程的地址空间策略来源，记录映射范围、权限、文件引用和物化 accounting；共享表只负责已经装入的页。

   备选方案是共享整个 VMA 或把共享状态放入 page/buffer cache block。共享 VMA 会把不同进程的虚拟地址布局绑在一起，影响 unmap/protection change；复用 block cache 则不能自然表达页级 zero-fill、PTE 生命周期和 frame 引用计数。因此选择页级共享对象。

2. 首次物化采用 lookup-then-load-then-publish 的流程。

   page fault 先验证 VMA 与权限，再按共享键查找已装入只读页。命中时仅增加 frame 引用并在当前进程页表发布 read-only PTE；未命中时从 page/buffer cache 读取文件内容到新用户 frame，完成尾页 zero-fill 后登记共享页，再发布 PTE。任何分配、读取或登记失败都必须回滚到 fault 前状态。

   备选方案是先发布私有页再尝试合并。该方案需要扫描现有映射并处理并发可见性，复杂度高于当前单核需求，也更难为未来 SMP 加锁。

3. 共享只读页复用现有 frame reference counting，并补充共享目录持有关系。

   物理 frame 的引用计数表示被多少地址空间或共享目录持有；共享目录项持有一份基础引用，每个映射 PTE 持有一份映射引用。unmap/teardown 清除 PTE 后递减映射引用；最后一个目录项引用释放时 frame 才能归还 allocator。目录项和 PTE 发布必须有明确顺序，避免成功路径引用计数少算，失败路径引用计数多留。

   备选方案是为 file-backed 页单独实现引用计数。这样会与 fork/COW 已有 frame_ref 语义并存，增加 double-free 或漏释放风险。

4. 写访问不进入 COW。

   共享只读 file-backed 页的 PTE 不带 writable，也不带 COW marker。CPL3 写 fault 必须按权限错误终止当前进程或返回现有用户 fault 行为，不能为该页分配私有副本，也不能修改 backing store。

   这保留了当前 bounded read mapping 的语义边界，避免把该 change 扩大为 writable mmap 或 POSIX private file mapping。

5. TLB 可见性只通过现有 boundary 表达。

   当前单核实现中，权限收紧、unmap 和 active-root PTE 替换仍调用本地 invalidation。接口调用必须携带地址空间 root、地址范围和完成顺序所需的信息；未来 SMP 可在同一 boundary 内扩展 target CPU set 和 shootdown completion，但本 change 不新增 APIC/IPI 依赖。

6. backing identity 使用 VFS 层统一 object id，而不是裸 `File` 指针。

   共享键中的 backing identity 由 VFS/backend 提供的稳定对象身份组成，至少区分 backend/device 或 mount identity，并包含 backend 内唯一的 `object_id`。`vfs::File *` 只作为 retain/release 的生命周期句柄，不能作为跨独立 `open()` 的共享键；否则两个进程分别打开同一路径时会因为 `File` 对象不同而无法共享。

   对 `/rw` backend，`object_id` 可映射到 inode-like identity，并在需要时扩展 generation 防止删除重建后的误复用。对只读 exFAT，`object_id` 应由只读介质上的稳定文件身份派生，例如 mount/device identity 加 first cluster、长度或等价目录项身份。测试用 synthetic file 也必须提供不会与真实 backend 冲突的 backend tag 与 object id。

7. 共享目录使用独立 bounded 上限。

   共享只读页目录是全局物化页表，容量维度是“可被跨进程复用的只读物化页数”。它不复用每进程 `MAX_VMAS`，也不复用 page/buffer cache 的 block cache 容量常量。实现应定义独立的 `MAX_SHARED_READONLY_PAGES` 或等价常量；容量耗尽时必须确定性失败并回滚，不能静默退回为私有成功映射。

8. exec 装载的只读 text/rodata 纳入本 change。

   ELF loader 需要把符合条件的只读 `PT_LOAD` segment 建成 file-backed lazy VMA，使 text/rodata 页在首次 fault 时通过共享目录 lookup/load/publish。可写 data/bss 仍保持私有进程语义：文件内初始数据可以按现有装载路径或后续私有 file-backed 设计处理，但不得把 writable segment 放入共享只读目录。exec staging 必须保留 rollback 语义：新 image 未 commit 前获取的共享目录引用、backing retain 和已发布页表状态都要能完整撤销。

## Risks / Trade-offs

- [共享键 identity 不稳定] → 使用 VFS 层统一 object id 加 backend/mount identity，并在 VMA/共享目录持有期间 retain backing file，避免 close/unlink 造成悬挂引用或跨 backend 误共享。
- [引用计数顺序错误导致提前释放或泄漏] → 把 publish/rollback 拆成明确阶段：frame allocated、shared entry retained、PTE installed、VMA accounting updated；每个失败点只回滚已完成阶段。
- [目录项容量耗尽] → 共享目录保持独立有界容量；容量不足时 deterministic failure 或退回不发布映射，不能静默创建私有成功语义。
- [尾页 zero-fill 与文件长度变化语义不清] → 以映射建立或物化时可观察的 bounded file length 为 mappable extent；超出范围的页内部分 zero-fill，写入和越界访问仍失败。
- [exec loader 范围扩大] → 只把只读 `PT_LOAD` text/rodata 转为共享 file-backed lazy VMA；writable data/bss、动态链接、共享库装载和完整 demand-paged ELF 策略仍不属于本 change。
- [未来 SMP retrofit 成本] → 共享目录访问和 frame_ref 更新从一开始标注所需锁/IRQ 边界，即使当前单核只用 bootstrap CPU 路径。

## Migration Plan

1. 扩展 VFS/backend object id、file-backed VMA、共享页元数据与 helper，但保持旧的只读映射请求返回语义不变。
2. 将 file-backed page fault 迁移为共享目录 lookup/load/publish 流程，并保留原有 deterministic failure。
3. 改造 ELF loader，使只读 text/rodata `PT_LOAD` segment 以 file-backed lazy VMA 表达，并让 exec commit/rollback 处理 staged shared references。
4. 更新 fork、unmap、protection change、teardown/reaper 对共享只读 PTE 的引用计数处理。
5. 增加默认关闭 smoke 和源码级检查，覆盖显式 mmap 共享与 exec text/rodata 共享；验证通过后再更新 roadmap checkbox。
6. 若实现中发现共享目录路径不稳定，可回滚到旧的 per-process file-backed materialization，同时保留 spec 中未完成任务不归档。

## Resolved Decisions

- 共享键使用 VFS/backend 统一 object id 加 backend/mount identity；`File` 指针只用于生命周期 retain，不作为跨进程共享 identity。
- 共享目录定义独立 bounded 上限，不复用每进程 VMA 上限或 page/buffer cache 容量。
- 本 change 同步覆盖显式只读 file-backed mapping 和 exec 装载的只读 text/rodata 共享；ELF loader 需要在本次实现中改造对应只读 segment 路径。
