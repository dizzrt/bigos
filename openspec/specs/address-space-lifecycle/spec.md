## Purpose

定义 BigOS 地址空间生命周期能力：跟踪运行时动态页表页 ownership，
在 leaf unmap 后回收空 PT/PD/PDPT 页，安全 teardown 已终止或 faulted 的用户地址空间，
并将用户退出或 fault 与资源回收分离到 safe reaper 边界。

## Requirements

### Requirement: 动态页表页 ownership 可追踪

BigOS SHALL record ownership metadata for runtime-created page-table pages, including their owning address-space category, page-table level, physical frame, and present-entry count or an equivalent invariant sufficient to determine whether the page-table page is empty. The metadata MUST distinguish reclaimable dynamic page-table pages from boot-stage, kernel image, direct-map, KVMEM, recursive self-mapping, and other static kernel-required page tables.

#### Scenario: map primitive 创建动态页表页

- **WHEN** a page-table map primitive allocates a new intermediate page-table page for kernel vmem or a user address-space root
- **THEN** BigOS MUST register that page-table page with its owner, level, frame address, and initial present-entry accounting
- **AND** the registration MUST complete before publishing a present descriptor that points to that page-table page

#### Scenario: 静态页表页不可回收

- **WHEN** a reclaim path observes a page-table page that belongs to boot handoff, kernel image mappings, high-half shared kernel mappings, direct map, KVMEM static setup, or recursive self-mapping requirements
- **THEN** BigOS MUST treat that page-table page as non-reclaimable
- **AND** it MUST NOT clear descriptors or free the physical frame for that page-table page

#### Scenario: ownership 元数据失败不发布半完成映射

- **WHEN** a map operation allocates a page-table page but cannot allocate or initialize the required ownership metadata
- **THEN** BigOS MUST roll back the page-table-page allocation or stop through a deterministic failure path
- **AND** it MUST NOT publish a present descriptor that points to an untracked reclaimable page-table page

### Requirement: 空页表页回收自叶子 unmap 向上执行

BigOS SHALL reclaim empty PT, PD, and PDPT pages after leaf mappings are removed, but only when the page-table page is dynamically owned by the target address space or kernel vmem range and contains no present entries. Each cleared leaf PTE or non-leaf descriptor MUST observe the current single-CPU TLB invalidation boundary.

#### Scenario: leaf PTE 清除后回收空 PT

- **WHEN** BigOS unmaps the last present leaf PTE from a dynamically owned PT page
- **THEN** it MUST clear the parent descriptor for that PT page
- **AND** it MUST release the PT physical frame only after confirming the PT page has no present entries and is reclaimable by ownership metadata
- **AND** it MUST invalidate the affected current-CPU translation or document that the unmapped root is inactive and will not be re-entered

#### Scenario: 空 PD 和 PDPT 向上回收

- **WHEN** clearing a child descriptor makes a dynamically owned PD or PDPT page empty
- **THEN** BigOS MUST clear the corresponding parent descriptor and release that empty page-table frame
- **AND** it MUST stop before clearing the PML4 root itself unless the caller is an explicit address-space teardown path

#### Scenario: 非空页表页不被释放

- **WHEN** a PT, PD, or PDPT page still contains at least one present entry after an unmap operation
- **THEN** BigOS MUST keep the page-table page and its parent descriptor present
- **AND** it MUST keep ownership accounting consistent for the remaining entries

### Requirement: 用户地址空间 teardown 释放 owned 资源

BigOS SHALL provide a user address-space teardown helper that releases resources owned by a terminated, faulted, exec-replaced, or reaped user process: user leaf physical pages, user low-half dynamic page-table pages, the user PML4 root, process kernel stack, and process-lifecycle-owned image metadata when they are no longer active. The helper MUST preserve borrowed kernel high-half mappings and MUST run only in a safe kernel context after process lifecycle rules permit final teardown.

#### Scenario: teardown 只遍历用户 owned 低半区

- **WHEN** BigOS tears down a user address-space root for a terminated, faulted, or exec-replaced process image
- **THEN** it MUST traverse only user-owned low-half mappings and page-table pages for that process
- **AND** it MUST NOT recursively free copied high-half kernel mappings, direct-map mappings, KVMEM mappings, recursive self-mapping entries, kernel image mappings, or boot handoff page tables

#### Scenario: 用户 leaf page 被释放

- **WHEN** teardown removes a process-owned user code, data, BSS, argument, environment, or stack mapping
- **THEN** BigOS MUST clear the user leaf PTE, invalidate the affected translation according to the single-CPU boundary, and return the owned physical page to the appropriate allocator
- **AND** it MUST NOT free physical pages that are only borrowed or shared by kernel mappings

#### Scenario: PML4 root 最后释放

- **WHEN** all owned low-half user mappings and dynamic child page-table pages have been released
- **THEN** BigOS MUST release the user PML4 root frame
- **AND** it MUST do so only after the active CR3 no longer points at that root

#### Scenario: process kernel stack 安全释放

- **WHEN** a terminated, faulted, or reaped process is being released
- **THEN** BigOS MUST release that process kernel stack only if the current stack pointer is not within the target stack range
- **AND** it MUST defer or fail safely if the process stack is still the active execution stack

#### Scenario: wait 状态消费后允许最终释放

- **WHEN** a zombie process has parent-visible status that has not yet been consumed by wait
- **THEN** BigOS MUST preserve enough process table and status metadata for the parent to observe the child result
- **AND** final process object and PID release MUST wait until status consumption or explicit orphan-reap policy allows it

### Requirement: 退出和用户 fault 只安排安全回收

BigOS SHALL separate user termination from resource reclamation. `SYS_EXIT`, user-mode page fault handling, invalid user-buffer handling, exec commit failure, and child termination MUST mark the affected process terminated, faulted, zombie, or reap-pending and arrange a later safe teardown; they MUST NOT immediately free the current kernel stack, active user root, process table entry, or process object on the same unsafe return path.

#### Scenario: SYS_EXIT 标记待回收

- **WHEN** a user process invokes `SYS_EXIT`
- **THEN** BigOS MUST record the exit code and mark the process terminated, zombie, or reap-pending according to its parent/wait ownership
- **AND** the syscall path MUST NOT return to the terminated user instruction stream
- **AND** it MUST NOT free the current process kernel stack or active CR3 root before switching to a safe kernel context

#### Scenario: 用户页错误标记 faulted

- **WHEN** a CPL3 page fault terminates the current user process
- **THEN** BigOS MUST record a deterministic fault reason and mark the process faulted, zombie, or reap-pending according to its parent/wait ownership
- **AND** it MUST preserve kernel-mode page fault diagnostic behavior for CPL0 faults
- **AND** it MUST NOT implement demand paging or resume the faulting user instruction as a successful recovery

#### Scenario: reaper 在安全上下文执行

- **WHEN** a reap-pending process or exec-replaced image is selected for teardown
- **THEN** BigOS MUST execute teardown from non-IRQ context with a safe kernel address-space root active
- **AND** it MUST verify that the resources being freed are not currently required by the executing stack, CR3 state, process table iteration, or parent wait status delivery

#### Scenario: child exit 唤醒 parent wait

- **WHEN** a child process becomes zombie while its parent is blocked in wait
- **THEN** BigOS MUST wake the parent through the kernel blocking primitive and make the child status observable
- **AND** it MUST keep resource reclamation separated from the wakeup path unless the wakeup path is already a documented safe reaper context

### Requirement: 地址空间 teardown 释放 VMA 元数据

BigOS SHALL release process-owned VMA metadata as part of safe user address-space teardown. VMA metadata cleanup MUST be ordered with user leaf page release, dynamic page-table reclamation, user PML4 release, and process object reaping so that no active path observes freed metadata.

#### Scenario: teardown 在安全上下文释放 VMA

- **WHEN** a terminated, faulted, exec-replaced, or reaped process image reaches the safe address-space teardown boundary
- **THEN** BigOS MUST make the process VMA collection unreachable from future user-memory validation before releasing its metadata
- **AND** teardown MUST run only after the active execution path no longer depends on that process VMA collection for syscall, exception, or user-copy handling

#### Scenario: VMA cleanup 不释放借用高半区

- **WHEN** VMA cleanup runs for a user process
- **THEN** BigOS MUST release only user process VMA metadata and user-owned low-half resources associated with those VMAs
- **AND** it MUST NOT treat borrowed kernel higher-half mappings, direct map entries, KVMEM mappings, recursive self-mapping entries, or boot handoff page tables as VMA-owned resources

### Requirement: VMA rollback 与页表 rollback 一致

BigOS SHALL keep VMA rollback consistent with page-table and physical-page rollback for failed `exec`, `brk`, anonymous mapping, and stack-growth operations.

#### Scenario: commit 前失败释放 staging VMA

- **WHEN** exec image preparation fails before the new image is committed
- **THEN** BigOS MUST release staging VMAs, staging user pages, staging dynamic page-table pages, and loader metadata allocated by the failed attempt
- **AND** it MUST preserve the old runnable process image and old VMA collection unchanged

#### Scenario: API 失败撤销已分配资源

- **WHEN** `brk`, anonymous mapping, or stack growth fails after allocating a physical page, page-table page, or VMA slot
- **THEN** BigOS MUST roll back the current operation's allocations and metadata changes or terminate the process through a documented safe teardown path
- **AND** it MUST NOT leave a successful return with VMA and page-table state disagreeing about the affected range

### Requirement: 地址空间生命周期验证可复现

BigOS SHALL provide reproducible validation for page-table ownership, empty page-table reclamation, user address-space teardown, and safe termination-to-reap handoff. Validation MUST separate source-level checks, builds, optional emulator smoke, unavailable environment blockers, historical diagnostics, and current-change issues.

#### Scenario: 源码级检查覆盖回收不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover dynamic page-table ownership metadata, present-entry accounting, rollback on metadata failure, empty PT/PD/PDPT reclamation, non-owned page-table rejection, active-root teardown rejection, and current-stack release deferral
- **AND** checks MUST confirm boot fixed addresses, higher-half base, direct-map window, `KVMEM_BASE`, recursive self-mapping, syscall vector, and exception/IRQ EOI semantics are not moved or widened by this change

#### Scenario: 构建与 emulator 验证被记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful `xmake` cross-toolchain build, relevant `uv run pytest` source-level checks, and strict OpenSpec validation for this change
- **AND** if Bochs runtime smoke cannot observe reclamation or user-exit markers due to emulator, ROM, serial/VGA oracle, image lock, or interaction limits, validation MUST record the missing dependency and remaining bootability risk

### Requirement: 主动 unmap 释放用户地址空间资源

BigOS SHALL release user address-space resources removed by an explicit anonymous unmap operation using the same ownership and safety rules as process teardown. The release path MUST clear affected leaf PTEs, release owned frames or decrement COW/shared references, reclaim empty dynamic page-table pages when ownership metadata permits, and invalidate affected current-CPU translations through the existing invalidation boundary.

#### Scenario: unmap 释放 owned 匿名页

- **WHEN** explicit unmap removes a materialized anonymous page exclusively owned by the current process
- **THEN** BigOS MUST clear the leaf PTE, invalidate the affected current-CPU translation, and return the owned frame through the appropriate allocator path
- **AND** it MUST update VMA/materialization accounting before the range can be considered unmapped

#### Scenario: unmap 递减 COW 或共享只读引用

- **WHEN** explicit unmap removes a materialized page that is COW-shared or otherwise reference-counted between processes
- **THEN** BigOS MUST decrement the frame reference count and return the frame only when the count reaches zero
- **AND** another process that still maps the frame MUST continue to observe valid contents and MUST NOT see a premature free

#### Scenario: unmap 后回收空页表页

- **WHEN** clearing unmapped leaf PTEs leaves a dynamically owned PT, PD, or PDPT page empty
- **THEN** BigOS MUST reclaim that page-table page according to the existing ownership metadata and empty-page-table reclamation rules
- **AND** it MUST NOT clear or free boot, kernel higher-half, direct-map, KVMEM, recursive self-mapping, or other non-owned static page tables

### Requirement: protection change 遵守 TLB invalidation 与页表权限边界

BigOS SHALL apply protection changes to present user PTEs in a way that never grants page-table access wider than VMA policy and observes the existing TLB invalidation boundary. Permission reductions MUST invalidate affected current-CPU translations before returning success to user mode.

#### Scenario: present PTE 权限被收紧

- **WHEN** protection change removes write or execute access from a range containing present user PTEs
- **THEN** BigOS MUST update those PTEs to permissions no wider than the new VMA permissions
- **AND** it MUST invalidate affected current-CPU translations before the syscall returns success

#### Scenario: 未物化页记录新权限

- **WHEN** protection change covers registered-but-unmaterialized anonymous pages
- **THEN** BigOS MUST update VMA permissions so future demand paging materializes those pages with the new permissions
- **AND** it MUST NOT allocate or map pages solely to apply the protection change

### Requirement: lifecycle operations run only from safe process context

BigOS SHALL execute explicit anonymous unmap and protection-change operations only from safe process syscall context where allocation, page-table modification, process lookup, and TLB invalidation are permitted. The operations MUST NOT run from IRQ context, scheduler critical sections, reaper teardown of another active process, or while the target CR3/stack state makes resource release unsafe.

#### Scenario: ordinary syscall context may modify current address space

- **WHEN** the current running user process invokes anonymous unmap or protection change from ordinary syscall context
- **THEN** BigOS MAY modify that process's VMA collection and user low-half page tables after all validation and staging succeeds
- **AND** it MUST preserve the active kernel stack, syscall return path, and process table iteration safety

#### Scenario: unsafe context is rejected

- **WHEN** anonymous unmap or protection-change logic is reached from IRQ context, a preemption-disabled scheduler critical section, or another context that cannot safely allocate or modify the current process address space
- **THEN** BigOS MUST return a deterministic error or enter a documented diagnostic path
- **AND** it MUST NOT free the active kernel stack, tear down the active CR3 root, perform blocking work, or publish partial page-table changes

### Requirement: 地址空间释放共享只读页引用

BigOS SHALL release shared read-only file-backed page references during explicit unmap, protection-change removal, exec replacement, user fault teardown, process exit, and safe reaper teardown. Releasing one address space MUST clear only that address space's PTEs and references; the shared frame MUST remain valid while another process, VMA, or shared directory entry still retains it.

#### Scenario: unmap 一个进程不释放他人共享页

- **WHEN** one process unmaps a range containing a materialized shared read-only file page while another process still maps the same shared frame
- **THEN** BigOS MUST clear the first process's PTE, invalidate the affected translation through the existing boundary, and drop only the first process's frame reference
- **AND** the second process MUST continue to read the same content from its still-valid read-only mapping

#### Scenario: safe teardown 释放最后引用

- **WHEN** the last process mapping a shared read-only file page exits or is reaped and no shared directory entry retains the frame
- **THEN** BigOS MUST release the final frame reference and return the frame through the appropriate allocator path
- **AND** it MUST do so only from a safe kernel context that is allowed to modify page tables and release user frames

### Requirement: fork 和 exec 保持共享只读生命周期一致

BigOS SHALL keep shared read-only file-backed pages reference-count-correct across `fork` and `exec`. `fork` MUST duplicate VMA metadata and retain existing shared read-only PTE frames without deep-copying them. `exec` MUST represent compatible read-only ELF text/rodata load segments as shared-capable file-backed mappings, stage new shared mappings, and publish them only after image replacement can commit; failed exec MUST release staged references without changing the old runnable image.

#### Scenario: fork 继承共享只读页引用

- **WHEN** `fork` duplicates a parent that has present shared read-only file-backed PTEs
- **THEN** BigOS MUST install child read-only PTEs that reference the same frames and increment the corresponding frame references
- **AND** it MUST NOT mark those file-backed PTEs COW or make them writable

#### Scenario: fork 失败回滚共享引用

- **WHEN** `fork` fails after retaining one or more shared read-only file-backed frames for the child
- **THEN** BigOS MUST drop the child-side references and release any child VMA metadata already staged
- **AND** the parent process's mappings and frame references MUST remain unchanged

#### Scenario: exec commit 与 rollback 处理共享引用

- **WHEN** exec prepares file-backed read-only mappings for a new image, including compatible read-only ELF text/rodata segments
- **THEN** BigOS MUST retain any staged shared page references only until commit succeeds
- **AND** if exec fails before commit, it MUST release all staged shared references while preserving the old image, old VMA collection, and old mappings

#### Scenario: exec 不共享 writable segment

- **WHEN** exec loads an ELF segment that has writable permissions or requires process-private bss/data semantics
- **THEN** BigOS MUST keep that segment out of the shared read-only page table
- **AND** it MUST preserve private writable process semantics while still allowing compatible read-only text/rodata segments from the same executable to share

### Requirement: protection change 不扩大共享只读权限

BigOS SHALL apply protection changes to ranges containing shared read-only file-backed pages without granting permissions wider than the VMA policy or shared page attributes. Permission reductions MUST update present PTEs and invalidate affected translations before returning success; permission increases that would make a shared read-only file page writable MUST fail deterministically.

#### Scenario: 收紧共享只读页权限

- **WHEN** protection change removes read or execute permission from a range containing present shared read-only file-backed PTEs
- **THEN** BigOS MUST update or clear the affected PTEs according to the new VMA policy
- **AND** it MUST invalidate affected current-CPU translations through the existing boundary before returning success to user mode

#### Scenario: 试图把共享只读页变为 writable

- **WHEN** protection change requests writable permission for a shared read-only file-backed page
- **THEN** BigOS MUST reject the request deterministically
- **AND** it MUST NOT mutate the shared page entry, backing file, or existing read-only mappings
