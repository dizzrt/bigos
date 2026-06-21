## ADDED Requirements

### Requirement: VMA-visible page-table changes use shootdown completion
BigOS SHALL route user page-table changes caused by VMA operations through the active TLB invalidation boundary owned by the affected `mm context`. When real SMP execution is enabled, changes that may leave stale translations on remote CPUs MUST require cross-CPU shootdown completion before the operation can release frames, reuse address ranges, or return to user mode.

#### Scenario: anonymous unmap waits for shootdown
- **WHEN** a process unmaps an anonymous/private VMA range whose page-table entries may be cached on another online CPU
- **THEN** BigOS MUST clear the affected PTEs, publish the page-table updates, and complete required TLB shootdown before releasing owned frames or reporting unmap success
- **AND** unrelated VMAs and mappings MUST remain unchanged

#### Scenario: protection change waits for shootdown
- **WHEN** a VMA protection change removes write, user, executable, or present access from existing PTEs
- **THEN** BigOS MUST complete local or cross-CPU TLB invalidation according to `mm context` residency before exposing the new protection state to user execution
- **AND** it MUST NOT allow a remote CPU to keep using a stale translation with permissions no longer granted by the VMA

#### Scenario: teardown distinguishes active and inactive roots
- **WHEN** address-space teardown clears PTEs for an address-space root that cannot be re-entered by any CPU before destruction
- **THEN** BigOS MAY avoid remote shootdown only if `mm context` residency proves no online CPU can still legally execute with that root
- **AND** otherwise teardown MUST use the cross-CPU shootdown boundary before reclaiming frames or page-table pages

### Requirement: VMA lifecycle is owned by mm context
BigOS SHALL bind each user VMA collection and address-space page-table root to an independent `mm context`. Process lifecycle operations MUST update `mm context` references before publishing or destroying VMA-visible state.

#### Scenario: exec publishes a new mm context
- **WHEN** exec commits a new user image
- **THEN** BigOS MUST publish the new VMA collection and page-table root through a referenced `mm context`
- **AND** the old `mm context` MUST be released only after no CPU residency or pending shootdown can observe it

#### Scenario: fork duplicates through mm context ownership
- **WHEN** fork duplicates a process address space
- **THEN** BigOS MUST create or reference the child `mm context` before exposing the child as runnable
- **AND** COW and VMA metadata MUST remain valid until both parent and child `mm context` references and shootdown dependencies are accounted for

#### Scenario: exit releases mm context after residency drains
- **WHEN** the last process reference to an `mm context` exits or is reaped
- **THEN** BigOS MUST mark the context for teardown and reclaim VMA/page-table resources only after active CPU residency and pending shootdown references have drained
- **AND** it MUST fail closed if the residency state cannot be made safe within the bounded diagnostic policy

### Requirement: COW and fault recovery respect remote translations
BigOS SHALL preserve copy-on-write and demand-fault correctness under real SMP by invalidating stale writable or missing translations on every CPU that may run the affected address space.

#### Scenario: COW write fault invalidates stale parent mapping
- **WHEN** a COW write fault changes a shared read-only COW PTE into a private writable PTE for the current process
- **THEN** BigOS MUST publish the new PTE and invalidate stale translations for the affected address on all CPUs that may run that process address space
- **AND** it MUST NOT make the old shared frame reclaimable until all mappings and references are accounted for

#### Scenario: demand materialization publishes before return
- **WHEN** a user page fault materializes a new anonymous, stack, heap, or file-backed read page
- **THEN** BigOS MUST publish frame contents and PTE state with ordering compatible with remote CPU observation
- **AND** any invalidation required by replacing an old non-present or permission state MUST complete before returning to user mode

### Requirement: VMA validation records SMP invalidation coverage
BigOS SHALL validate VMA operations that affect page tables with source checks and emulator smoke covering shootdown integration when local tooling supports it.

#### Scenario: source checks cover VM invalidation call sites
- **WHEN** this change is implemented
- **THEN** validation MUST include source-level checks or review notes for unmap, protection change, teardown, COW, demand fault, and exec replacement invalidation call sites
- **AND** checks MUST confirm that frame release after PTE removal is ordered after required invalidation completion

#### Scenario: runtime smoke covers userland after VM shootdown
- **WHEN** QEMU multi-core validation and the dedicated TLB shootdown smoke build switch are available
- **THEN** validation MUST include a bounded VM smoke that exercises `mm context` residency and page-table changes while multiple CPUs are online
- **AND** the smoke MUST not claim broad POSIX `mmap`, shared writable mapping, or complete file-backed mmap semantics
