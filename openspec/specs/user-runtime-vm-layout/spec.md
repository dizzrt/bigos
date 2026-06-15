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

BigOS SHALL reserve explicit layout and metadata extension points for future dynamic linking without implementing dynamic linking in this change. The runtime layout MAY reserve bounded gaps or metadata fields for a future interpreter, shared-object area, or loader handshake, but current execution MUST continue to reject unsupported dynamic-linking features.

#### Scenario: dynamic ELF feature remains rejected

- **WHEN** a user ELF image requires `PT_INTERP`, dynamic relocation, shared-object loading, `ET_DYN` execution, or a runtime dynamic loader
- **THEN** BigOS MUST reject the image with a deterministic loader or exec error
- **AND** it MUST NOT enter ring3 with a partially interpreted dynamic image

#### Scenario: reserved future area does not grant access

- **WHEN** the runtime layout contains a reserved future-runtime gap
- **THEN** BigOS MUST leave the gap unmapped and uncovered by writable/executable VMAs until a later explicit capability defines its semantics
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
