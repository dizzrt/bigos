## Purpose

Define lifecycle requirements for BigOS slab allocator behavior, including dynamic slab reclaim, page-backed large allocations, debug guard coverage, validation statistics, and explicit perfect-fit semantics.

## Requirements

### Requirement: Slab allocator reclaims empty dynamic slabs

The slab allocator SHALL reclaim fully empty dynamic slabs when the cache reclaim policy allows it, while preserving permanent bootstrap slabs.

#### Scenario: Dynamic slab becomes empty

- **WHEN** the last allocated object in a non-permanent dynamic slab is freed
- **THEN** the slab allocator may remove that slab from cache lists and release its backing pages and metadata according to the reclaim policy

#### Scenario: Permanent slab becomes empty

- **WHEN** all objects in a `SLAB_PERMANENT` slab are free
- **THEN** the slab remains owned by its cache and is not returned to `free_pages()`

### Requirement: kmalloc supports page-backed large allocations

`kmalloc(size)` SHALL support requests larger than the configured small-object slab cache range by allocating page-backed kernel virtual memory and marking it so `free()` can release it correctly.

#### Scenario: Large allocation succeeds

- **WHEN** `kmalloc(size)` is called with `size` greater than the largest small-object cache and sufficient pages are available
- **THEN** it returns a mapped writable kernel virtual address backed by enough pages for the requested size and allocation metadata

#### Scenario: Large allocation is freed

- **WHEN** `free(ptr)` receives a pointer returned by the large allocation path
- **THEN** it identifies the allocation as page-backed and releases the corresponding kernel virtual pages exactly once

### Requirement: Slab debug guard detects invalid lifecycle operations

The slab allocator SHALL provide optional debug guard coverage for invalid frees, double frees, object boundary violations, and freed-object poisoning in validation builds.

#### Scenario: Double free is detected in debug build

- **WHEN** debug guard is enabled and `free(ptr)` is called twice for the same slab object
- **THEN** the allocator detects the second free and reports a deterministic failure rather than corrupting cache state

#### Scenario: Freed object is poisoned in debug build

- **WHEN** debug guard is enabled and a slab object is freed
- **THEN** the allocator writes a deterministic poison pattern to the object payload after updating ownership metadata safely

### Requirement: Slab statistics are observable for validation

The slab allocator SHALL expose validation-oriented statistics for caches, slabs, small objects, large allocations, and reclaim events without requiring scheduler, IRQ, SMP, or hosted runtime support.

#### Scenario: Cache statistics are printed or collected

- **WHEN** memory diagnostics are requested in a validation build
- **THEN** the allocator reports each cache size, slab counts, object counts, free/full status, and dynamic reclaim counts

#### Scenario: Large allocation statistics are tracked

- **WHEN** page-backed large allocations are allocated and freed
- **THEN** validation statistics reflect outstanding large allocation count and pages held by large allocations

### Requirement: Perfect-fit flag semantics are explicit

The slab allocator SHALL either implement dynamic perfect-fit cache creation or explicitly reject unsupported perfect-fit cache creation requests with deterministic behavior and source-level tests.

#### Scenario: Perfect-fit cache creation is supported

- **WHEN** `GFM_PERFECT_FIT` requests a size with no existing cache and dynamic cache creation is implemented
- **THEN** the allocator creates a matching cache or fails without partial cache publication

#### Scenario: Perfect-fit cache creation is not supported

- **WHEN** dynamic perfect-fit cache creation remains out of scope
- **THEN** the allocator rejects such requests deterministically and tests prevent code from assuming automatic cache creation
