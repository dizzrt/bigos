# 地址空间生命周期与回收

本阶段为运行时创建的页表和默认关闭的首个用户程序补充 bounded 回收能力。除非被明确登记为动态 owned 页表页，否则 boot/kernel/direct-map/KVMEM 静态映射都保持 borrowed 语义。

## 页表 ownership

`kernel/mm/vmem.cc` 维护一个静态 `PageTableMetadata` 表，记录：

- owner 类别：kernel vmem 动态映射或派生用户地址空间。
- 页表级别：PML4、PDPT、PD 或 PT。
- 所属 root 物理帧和页表物理帧。
- 用于回收判断的 present-entry count。

metadata 会在 present descriptor 发布前完成登记。如果 metadata 登记、页表页分配、direct-map 访问或 leaf 发布失败，本次 map 操作会回滚新建 descriptor 并返回失败，不留下未追踪的可回收页表页。

静态或 borrowed 页表不会进入该 registry。回收路径把缺失 metadata 视为不可回收，从而保留 boot handoff 页表、kernel image 映射、复制的高半区 kernel 条目、direct map、KVMEM 静态建立部分和 recursive self-mapping 依赖。

## 空页表回收

`unmap_page()` 清除 present leaf PTE，执行同 CPU `invlpg`，递减 PT present-entry count，然后向上检查 PT、PD 和 PDPT。页表帧仅在满足以下条件时释放：

- 拥有与目标 root/owner 匹配的 ownership metadata。
- present-entry count 为零。
- 父 descriptor 已先被清除，再把该帧归还 buddy allocator。

公开 `alloc_kernel_pages(nr_pages, flags)` / `free_pages(ptr)` API 仍保持页数语义。KVMEM 释放仍先 unmap 再归还物理 backing，空页表回收位于 unmap 边界之后。

## 用户 teardown

`teardown_user_address_space(root)` 仅在目标 root 不是 active CR3 root 时释放派生用户 root。它只遍历 PML4 index `0..255`，因此复制的高半区 kernel 映射保持 borrowed 且不会被触碰。

对每个 user-owned 低半区映射，teardown 会清除 leaf PTE、归还 process-owned leaf 物理页、释放空 PT/PD/PDPT 帧，并最后释放用户 PML4 root。teardown 时 root 已非活动，teardown 开始后不得再次激活该 root；active-root 或 remote-use 情况必须在回收前被拒绝，或通过 SMP TLB-shootdown 边界处理。

## 进程 reaper

`SYS_EXIT`、用户态 `#PF` 和非法用户 buffer 只标记进程 terminated 或 faulted，记录 exit/fault 信息，恢复安全 kernel root，并进入 scheduler exit 路径。它们不会在同一条不安全返回路径上回收 active syscall/fault stack 或 active user root。

`BIGOS_USER_PROGRAM_SMOKE` 下，scheduler idle loop 调用 `reap_pending_processes()`。reaper 在非 IRQ kernel context 中运行，检查目标进程 kernel stack 不是当前栈，拒绝 active-root teardown，然后释放用户地址空间和 kernel stack。成功回收输出 `BIGOS_USER_RECLAIMED`；不安全 stack/root 状态输出确定性 defer marker。

## 验证

源码级验证位于 `tests/test_address_space_lifecycle_source.py`，覆盖：

- 动态页表 metadata、publish 顺序、rollback 和 present-entry accounting。
- 空 PT/PD/PDPT 回收，以及通过 metadata 缺失拒绝 non-owned 页表。
- 只遍历用户低半区的 teardown、高半区 borrowed 保留、active-root 拒绝和 PML4 最后释放。
- `SYS_EXIT`、用户 `#PF` 和非法用户 buffer 到 safe reaper 的 handoff。

本 change 的构建与运行期验证记录在 `openspec/changes/archive/2026-06-07-reclaim-address-space-page-tables/validation.md`。
