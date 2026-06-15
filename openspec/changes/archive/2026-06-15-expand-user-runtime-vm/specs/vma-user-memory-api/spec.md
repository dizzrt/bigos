## ADDED Requirements

### Requirement: VMA purpose matches runtime layout region

BigOS SHALL require each user VMA to declare a purpose and backing that matches exactly one allowed region class in the committed runtime VM layout. VMA insertion for exec, `brk`, restricted anonymous mapping, stack growth, or argument setup MUST reject ranges that collide with reserved gaps or incompatible region purposes.

#### Scenario: VMA insertion validates region purpose

- **WHEN** process image preparation or a user memory API creates a VMA
- **THEN** BigOS MUST validate that the requested range belongs to an allowed runtime layout region and that the VMA purpose, permissions, backing type, and growth policy match that region
- **AND** the VMA MUST NOT overlap another VMA or a reserved future-runtime gap

#### Scenario: incompatible VMA request fails without publication

- **WHEN** a `brk`, restricted anonymous mapping, stack-growth registration, or exec image setup request would create a VMA in an incompatible runtime layout region
- **THEN** BigOS MUST reject the operation deterministically
- **AND** it MUST leave the previous VMA collection, mapped pages, materialization accounting, and visible process state unchanged unless the process has entered a documented fatal path

### Requirement: materialization accounting is shared by VM operations

BigOS SHALL use one materialization accounting model for lazily backed heap, restricted anonymous mappings, downward-growing stack pages, and ELF zero-fill ranges. The accounting MUST be sufficient for demand paging, fork/COW duplication, range validation, shrink/unmap, exec replacement, and process teardown.

#### Scenario: lazy-backed regions share accounting

- **WHEN** heap, anonymous mapping, stack, or ELF zero-fill memory is registered as lazily backed
- **THEN** BigOS MUST record enough VMA metadata to distinguish registered-but-unmaterialized pages from materialized pages
- **AND** first access, fork/COW, shrink, unmap, and teardown MUST update or consume that metadata consistently

#### Scenario: accounting failure rolls back operation

- **WHEN** a VM operation cannot allocate or update required materialization accounting
- **THEN** BigOS MUST fail the operation before exposing the new layout or mapping to user mode
- **AND** any pages, PTEs, or VMA records from the failed operation MUST be released or routed through the documented safe-release path

### Requirement: user range validation respects runtime layout

BigOS SHALL validate user-provided ranges against both the VMA collection and the committed runtime layout before copying data between kernel and user memory. A present PTE alone MUST NOT authorize access if the range is outside the runtime layout or violates the VMA purpose and permissions.

#### Scenario: user copy rejects reserved gap

- **WHEN** syscall handling needs to read from or write to a user pointer range that overlaps a runtime-reserved gap
- **THEN** BigOS MUST reject the range or terminate the current user process through the documented user fault path
- **AND** it MUST NOT read or write arbitrary kernel memory or materialize the reserved gap

#### Scenario: user copy accepts compatible materialized range

- **WHEN** a user pointer range is fully covered by compatible VMAs, allowed by the runtime layout, and backed by present user-accessible mappings or an equivalent safe-copy path
- **THEN** BigOS MUST allow the bounded copy according to the requested access mode
- **AND** the copy MUST NOT grant write access to read-only, executable-only, guard, or reserved ranges
