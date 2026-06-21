## ADDED Requirements

### Requirement: Shared read-only PTE removal completes cross-CPU invalidation
BigOS SHALL use cross-CPU TLB shootdown completion for shared read-only mapping PTE removal when any online CPU may hold a stale translation for the affected address space. Shared frame reclamation MUST occur only after all relevant PTEs are cleared and required invalidation has completed.

#### Scenario: shared page unmap targets resident CPUs
- **WHEN** BigOS unmaps a shared read-only file-backed page from an address space that may be active on another online CPU
- **THEN** it MUST clear the affected PTE, publish the page-table update, and complete TLB shootdown for all resident target CPUs
- **AND** it MUST NOT decrement the shared frame to reclaimable state while any still-present PTE can legally reference it

#### Scenario: inactive address space avoids unnecessary IPI
- **WHEN** BigOS removes shared read-only PTEs from an inactive address-space root that no online CPU can re-enter before teardown or CR3 reload
- **THEN** it MAY avoid sending remote shootdown IPIs
- **AND** the decision MUST be based on explicit address-space residency or teardown state

### Requirement: Shared mapping metadata uses IRQ-safe publication ordering
BigOS SHALL publish shared read-only mapping metadata, frame references, and PTE changes in an order compatible with future and active remote CPU observation. Metadata removal MUST not make the frame reclaimable before PTE removal and required invalidation are complete.

#### Scenario: shared page publish orders contents before PTE
- **WHEN** BigOS materializes a shared read-only file page
- **THEN** it MUST finish loading contents, record shared metadata, retain the frame, and publish the user PTE in that order
- **AND** the publication ordering MUST be expressed through the selected lock, atomic operation, interrupt boundary, or architecture fence

#### Scenario: shared page removal orders PTE before frame release
- **WHEN** the last known mapping of a shared read-only page is removed
- **THEN** BigOS MUST clear relevant PTEs and complete required invalidation before making the frame reclaimable
- **AND** it MUST preserve page/buffer cache integrity for other processes and mappings that still retain the backing object

### Requirement: Shared mapping validation covers SMP invalidation
BigOS SHALL validate shared read-only mapping behavior under the active SMP shootdown boundary without claiming broad file-backed mmap completeness.

#### Scenario: source checks cover shared mapping lifecycle
- **WHEN** this change is implemented
- **THEN** validation MUST include source-level checks or review notes for shared read-only PTE publication, removal, frame reference ordering, and shootdown completion before reclaim

#### Scenario: multi-core smoke remains bounded
- **WHEN** runtime validation with multiple CPUs is available
- **THEN** validation MUST exercise shared read-only mapping unmap or teardown with remote CPU invalidation where practical
- **AND** validation MUST NOT claim shared writable mappings, writable file-backed mmap, or full POSIX mmap behavior
