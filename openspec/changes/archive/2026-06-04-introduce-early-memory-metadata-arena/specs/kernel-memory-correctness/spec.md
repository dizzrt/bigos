## ADDED Requirements

### Requirement: Buddy bootstrap does not depend on ordinary kmalloc growth

Buddy initialization SHALL be able to model usable BootInfo memory regions using bootstrap metadata storage without requiring ordinary slab/kmalloc dynamic slab growth before VMem is initialized.

#### Scenario: Complex memory map consumes bootstrap metadata
- **WHEN** BootInfo contains multiple usable memory regions requiring several PageBlock records
- **THEN** buddy initialization creates required metadata through the early metadata arena rather than relying on dynamic slab expansion

#### Scenario: Bootstrap metadata failure preserves allocator invariants
- **WHEN** metadata storage is insufficient during buddy initialization
- **THEN** buddy initialization fails explicitly without publishing inconsistent free page counts or corrupted free lists

#### Scenario: Runtime buddy split remains normal allocator backed
- **WHEN** buddy performs a split after `init_mem()` has completed
- **THEN** runtime split metadata may use the normal allocator path and remains covered by existing split failure rollback requirements
