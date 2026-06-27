## Purpose

定义 BigOS 用户程序运行时虚拟内存布局能力：为每个已提交用户进程镜像维护有界 runtime VM layout，显式区分 ELF 映射、heap、受限匿名映射、用户栈、stack guard/growth、初始参数区和未来 runtime 保留 gap；并将该布局作为 VMA、页表映射、缺页恢复、exec commit 与 teardown 的共同边界。该能力不实现动态链接、共享库、runtime dynamic loader、广泛 file-backed `mmap` 或完整 POSIX 虚拟内存模型。

## Requirements

### Requirement: 用户运行时地址空间布局

BigOS SHALL define a bounded user runtime virtual-memory layout for each committed user process image. The layout MUST distinguish executable ELF mappings, writable data/BSS, heap, restricted anonymous mappings, user stack, stack guard/growth range, runtime metadata/argument area, and reserved future-runtime gaps. All user regions MUST remain in the supported user low-half range and MUST avoid the kernel higher half, direct map, KVMEM, and recursive self-mapping ranges.

#### Scenario: committed image publishes a complete layout

- **WHEN** an ELF exec or process image commit succeeds
- **THEN** BigOS MUST publish a complete runtime layout description covering ELF load ranges, heap seed, anonymous mapping cursor, stack, guard/growth range, initial argument area, and reserved future-runtime gaps
- **AND** every published user range MUST be page-aligned where required, non-overlapping, within the supported user low-half range, and compatible with the VMA collection for the process

#### Scenario: layout conflict rejects commit

- **WHEN** image preparation detects an overflow, overlap, unsupported alignment, kernel-range collision, stack collision, or runtime-reserved-range collision
- **THEN** BigOS MUST reject the image before publishing the new process layout
- **AND** any allocated pages, page-table pages, kernel buffers, or VMA metadata from the failed attempt MUST be released or routed through the documented safe-release path

### Requirement: image commit is atomic from user-visible state

BigOS SHALL separate runtime image preparation from process-visible commit. Preparation MAY allocate kernel buffers, user pages, page-table pages, VMA records, and initial stack data, but the target process MUST observe the new image only after all required layout, VMA, page-table, entry point, stack, and ownership checks succeed.

#### Scenario: preparation failure keeps old image

- **WHEN** exec image preparation fails before commit for a process that already has a runnable image
- **THEN** BigOS MUST preserve the old process image, address-space root, VMA collection, file-descriptor state, signal state, and lifecycle state unless the process has entered a documented fatal exec path
- **AND** partially prepared resources MUST NOT remain reachable as successful user mappings

#### Scenario: commit publishes new image once

- **WHEN** all image preparation checks have succeeded
- **THEN** BigOS MUST publish the new runtime layout, VMA collection, page-table root, initial user stack pointer, entry point, and ownership records as one commit boundary
- **AND** teardown of the previous image MUST follow the safe process lifecycle or reaper rules without freeing borrowed kernel high-half page tables

### Requirement: future dynamic-linking preparation remains non-runtime

BigOS SHALL define explicit, bounded layout and metadata extension points for dynamic linking and SHALL, only on a default-off bounded dynamic-link path, use a bounded interpreter mapping region, shared-object mapping region, and auxv metadata as live runtime layout. 当动态链接路径关闭时，运行时布局 MUST 把这些预留 gap 保持为未映射的非运行时区域并继续拒绝不受支持的动态特性。即使动态路径启用，所有新增动态映射区 MUST 落在受支持用户低半区，MUST 与既有 ELF 映射、heap、受限匿名映射、用户栈、guard/growth、参数区互不重叠，且超出有界动态子集的特性 MUST 仍被拒绝。

#### Scenario: 动态路径关闭时 gap 仍非运行时

- **WHEN** 动态链接路径关闭，且某用户 ELF 镜像需要 `PT_INTERP`、动态重定位、共享对象加载、`ET_DYN` 执行或运行时动态加载器
- **THEN** BigOS MUST 以确定性 loader/exec 错误拒绝该镜像
- **AND** BigOS MUST NOT 以部分解释的动态镜像进入 ring3，预留 gap MUST 保持未映射

#### Scenario: 动态路径启用时 gap 成为有界运行时映射区

- **WHEN** 动态链接路径启用并加载含 `PT_INTERP` 的有界 `ET_DYN` 主镜像
- **THEN** BigOS MUST 把解释器映射区与共享对象映射区放入既有预留运行时 gap 内，并纳入 runtime VM 布局描述与 VMA 集合
- **AND** 所有新增动态映射区 MUST 页对齐、互不重叠、落在受支持用户低半区，并避免内核 higher-half/direct-map/KVMEM/自映射范围与栈冲突

#### Scenario: 动态布局冲突或超界拒绝提交

- **WHEN** 动态映射区准备时检测到 overflow、overlap、不支持的对齐、内核范围冲突、栈冲突，或动态子集之外的特性（TLS、`IFUNC`、符号版本、无界共享对象数）
- **THEN** BigOS MUST 在发布新进程布局前拒绝该镜像
- **AND** 失败尝试中已分配的页、页表页、内核缓冲或 VMA 元数据 MUST 经既有 safe-release 路径回收

#### Scenario: reserved future area does not grant access

- **WHEN** the runtime layout contains a reserved future-runtime gap not used by an enabled bounded dynamic-link path
- **THEN** BigOS MUST leave the gap unmapped and uncovered by writable/executable VMAs until an explicit capability defines its semantics
- **AND** user access to the gap MUST fail through the normal user fault path rather than materializing memory implicitly

### Requirement: runtime VM validation is reproducible

BigOS SHALL provide reproducible validation for the runtime VM layout, image commit boundary, dynamic-linking non-goals, and architecture-critical invariants.

#### Scenario: validation covers layout invariants

- **WHEN** this change is implemented
- **THEN** validation MUST cover non-overlap, user low-half bounds, stack/heap/anonymous ordering, reserved-gap behavior, atomic image commit, and failure rollback
- **AND** validation MUST confirm boot fixed addresses, higher-half base, direct-map window, KVMEM, recursive self-mapping, syscall vector, exception/IRQ gate privilege, and EOI semantics are not moved or widened

#### Scenario: unavailable runtime smoke is recorded

- **WHEN** QEMU, Bochs, cross-binutils, ROM/display configuration, serial observability, or disk image generation is unavailable
- **THEN** validation MUST record the skipped runtime smoke, substitute checks, and residual bootability risk
- **AND** OpenSpec strict validation for this change MUST still be recorded
