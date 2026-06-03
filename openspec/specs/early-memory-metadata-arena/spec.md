## Purpose

Define the early metadata arena used by BigOS memory initialization to allocate buddy bootstrap metadata without depending on ordinary slab/kmalloc dynamic growth before VMem is initialized.

## Requirements

### Requirement: Early metadata arena provides buddy bootstrap storage

BigOS SHALL provide an early memory metadata arena for buddy initialization metadata so that consuming BootInfo memory regions does not require ordinary slab/kmalloc dynamic growth.

#### Scenario: Buddy initialization allocates PageBlock metadata from arena

- **WHEN** buddy initialization creates `PageBlock` records for usable BootInfo memory regions
- **THEN** those initialization-time records are allocated from the early metadata arena or an explicitly equivalent bootstrap source

#### Scenario: Buddy initialization allocates list nodes from arena

- **WHEN** buddy initialization creates free-list nodes for initialization-time PageBlocks
- **THEN** those nodes are allocated from the early metadata arena or an explicitly equivalent bootstrap source

### Requirement: Early metadata arena has explicit lifetime and ownership

The early metadata arena SHALL have a documented initialization lifetime, and metadata allocated from it during buddy bootstrap SHALL remain valid for as long as buddy free/allocated lists reference it.

#### Scenario: Arena stops accepting new bootstrap allocations

- **WHEN** buddy initialization completes successfully
- **THEN** the early metadata arena is no longer used for new runtime allocator metadata allocations

#### Scenario: Buddy owns arena-backed metadata records

- **WHEN** a PageBlock created during initialization remains in a buddy list
- **THEN** its storage remains valid and is not returned to general kmalloc or VMem backing

### Requirement: Early metadata arena failure is diagnosable

The early metadata arena SHALL fail explicitly when capacity is insufficient and MUST NOT leave partially inserted buddy metadata in free lists.

#### Scenario: Arena capacity is exhausted

- **WHEN** buddy initialization cannot allocate required PageBlock or list-node metadata from the arena
- **THEN** initialization emits a clear memory metadata failure and halts safely or returns a fatal initialization failure

#### Scenario: Partial metadata creation fails

- **WHEN** arena allocation fails after part of a region has been modeled
- **THEN** allocator state remains consistent and the failure path does not publish half-initialized list nodes as allocatable memory

### Requirement: Early metadata arena preserves boot memory layout assumptions

The early metadata arena SHALL NOT overlap fixed boot handoff, boot-stage page-table, kernel load, kernel image, or self-mapping layout regions.

#### Scenario: Arena source avoids reserved low addresses

- **WHEN** the arena source is selected
- **THEN** it does not overlap legacy E820 buffers, BootInfo v1/v2 handoff areas, boot-stage page tables, or generated boot assets recorded in the x86 boot layout

#### Scenario: Arena source avoids kernel image memory

- **WHEN** buddy initializes usable physical memory
- **THEN** arena backing and kernel image memory are not released as ordinary allocatable pages
