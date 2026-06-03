## ADDED Requirements

### Requirement: Slab lifecycle operations preserve allocator invariants

Slab lifecycle operations SHALL preserve cache list membership, object accounting, backing page ownership, and `free()` dispatch correctness across small-object, large-object, reclaim, and failure paths.

#### Scenario: Empty slab reclaim preserves cache state
- **WHEN** an empty dynamic slab is reclaimed
- **THEN** it is removed from cache lists before its metadata and backing pages are released, and cache object counts remain consistent

#### Scenario: Large allocation failure rolls back state
- **WHEN** page-backed large allocation fails while allocating pages or metadata
- **THEN** `kmalloc()` returns `nullptr` and releases any partially acquired pages or metadata

#### Scenario: free dispatch distinguishes allocation kinds
- **WHEN** `free(ptr)` receives a pointer allocated by either slab small-object or page-backed large-object path
- **THEN** it identifies the allocation kind using validated metadata and executes the matching release path without corrupting the other allocator state
