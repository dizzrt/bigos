## ADDED Requirements

### Requirement: 用户地址空间遵守 runtime layout

BigOS SHALL require every user low-half mapping in a committed process address-space root to be authorized by both the process VMA collection and the committed runtime VM layout. Page-table helpers MUST NOT publish user PTEs that widen permissions beyond the VMA or place mappings inside runtime-reserved gaps, kernel higher-half ranges, direct map, KVMEM, or recursive self-mapping ranges.

#### Scenario: user mapping checks layout and VMA

- **WHEN** kernel code maps a user page for ELF segments, heap, restricted anonymous mapping, stack growth, argument setup, or future runtime metadata
- **THEN** BigOS MUST confirm the target page is covered by a compatible VMA and by an allowed region of the committed runtime layout before publishing a present PTE
- **AND** the PTE user, writable, and no-execute attributes MUST not grant access wider than the layout and VMA permit

#### Scenario: reserved gap mapping is rejected

- **WHEN** a user mapping request targets an unmapped runtime-reserved gap or an address outside the supported runtime layout
- **THEN** BigOS MUST reject the mapping or enter a deterministic failure path before publishing a present PTE
- **AND** any physical page or intermediate page-table page allocated by the failed operation MUST be released or left only in a documented fatal state

### Requirement: address-space teardown respects layout ownership

BigOS SHALL use runtime layout ownership and VMA materialization accounting to decide which user pages and low-half page-table pages can be reclaimed. Borrowed kernel high-half entries copied into a user root MUST remain non-owned by the process address space.

#### Scenario: teardown frees only owned low-half state

- **WHEN** a process image exits, faults, execs a replacement image, or is reaped
- **THEN** BigOS MUST reclaim only pages, mappings, and dynamically owned low-half page-table pages that belong to the process runtime layout and VMA collection
- **AND** it MUST NOT reclaim page-table pages reachable only through borrowed high-half kernel PML4 entries

#### Scenario: partial image preparation rolls back address-space state

- **WHEN** image preparation fails after allocating a user root, low-half page-table page, or mapped user page but before commit
- **THEN** BigOS MUST release the uncommitted address-space state without exposing it to the old or new user image
- **AND** rollback MUST preserve the currently active CR3 and kernel diagnostic path
