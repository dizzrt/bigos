## ADDED Requirements

### Requirement: VMA 集合按地址空间复制

BigOS SHALL support duplicating a process VMA collection and its materialization accounting as part of address-space copy on `fork`. The duplicate MUST preserve every VMA's range, purpose, backing, growth, permissions, and `materialized_start`/`materialized_end` accounting, as well as the heap base/break/limit and anonymous next-cursor bookkeeping, so the child observes the same user address-space layout as the parent at fork time.

#### Scenario: 复制 VMA 集合保留布局与记账

- **WHEN** a process address space is duplicated for `fork`
- **THEN** BigOS MUST copy the full VMA collection including per-VMA materialization accounting and the heap/anonymous bookkeeping fields
- **AND** the child MUST present the same VMA ranges, permissions, and growth policies as the parent

### Requirement: 可写匿名区间 fork 进入 COW 共享

BigOS SHALL make writable anonymous-backed ranges established through `brk` and restricted anonymous mapping enter copy-on-write sharing on `fork` rather than eager deep copy. Materialized writable anonymous pages MUST be shared read-only with a COW marker between parent and child, and unmaterialized lazy intervals MUST be carried as metadata only, to be materialized later through the existing unified page-fault path in whichever process first accesses them.

#### Scenario: brk/匿名区间 fork 时不深拷贝

- **WHEN** `fork` duplicates a process whose heap or anonymous mapping has materialized writable pages
- **THEN** BigOS MUST share those frames copy-on-write read-only between parent and child instead of allocating and copying new frames at fork time
- **AND** unmaterialized portions of the same intervals MUST be duplicated as VMA metadata without forcing materialization
