## ADDED Requirements

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
