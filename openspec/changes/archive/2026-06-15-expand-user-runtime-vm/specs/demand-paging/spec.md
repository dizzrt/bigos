## ADDED Requirements

### Requirement: 缺页恢复遵守 runtime layout

BigOS SHALL require the unified user page-fault handler to validate the faulting address against the committed runtime VM layout before materializing memory. A fault in a guard range, reserved future-runtime gap, kernel range, unsupported file-backed range, or out-of-layout address MUST NOT be recovered as anonymous memory.

#### Scenario: recoverable anonymous fault is in layout

- **WHEN** a CPL3 not-present fault targets a lazily backed heap, restricted anonymous, stack-growth, or ELF zero-fill page
- **THEN** BigOS MUST confirm the faulting page is covered by a compatible VMA and allowed by the committed runtime layout before allocating and mapping a user frame
- **AND** the mapped page MUST use permissions no wider than the VMA and layout allow

#### Scenario: reserved or guard fault is not materialized

- **WHEN** a CPL3 fault targets a runtime-reserved gap, stack guard region, kernel range, unsupported file-backed range, or address outside the committed runtime layout
- **THEN** BigOS MUST terminate the current user process through the documented user fault path
- **AND** it MUST NOT silently convert the fault into a successful anonymous materialization

### Requirement: COW faults preserve runtime ownership

BigOS SHALL handle copy-on-write write faults only for pages whose VMA and runtime layout region both permit writable anonymous ownership. COW split or in-place re-enable MUST preserve materialization accounting, frame ownership, and teardown behavior for the affected process.

#### Scenario: valid COW write fault splits owned page

- **WHEN** a CPL3 write fault sets the present bit on a read-only COW-marked page in a writable anonymous-backed runtime layout region
- **THEN** BigOS MUST route the fault to the COW branch, allocate or reuse a frame according to the COW ownership rules, restore writable non-executable user permissions, and resume the faulting instruction
- **AND** the VMA materialization accounting and process-owned frame records MUST remain consistent for later fork, exec, exit, and reap

#### Scenario: invalid COW candidate is killed

- **WHEN** a CPL3 present-bit fault targets a page that is not COW-marked, is outside a writable anonymous runtime region, or violates VMA permissions
- **THEN** BigOS MUST preserve deterministic process termination through the documented user fault path
- **AND** it MUST NOT treat a genuine protection violation as a successful COW split

### Requirement: page-fault validation records layout boundaries

BigOS SHALL validate that demand-paging behavior respects runtime layout boundaries and preserves kernel fault diagnostics.

#### Scenario: validation covers runtime-layout faults

- **WHEN** this change is implemented
- **THEN** validation MUST cover successful lazy materialization inside allowed runtime layout regions and deterministic failure for reserved gaps, guard ranges, permission violations, and invalid COW candidates
- **AND** validation MUST confirm CPL0 page faults remain on the kernel diagnostic/panic path rather than user demand-paging recovery
