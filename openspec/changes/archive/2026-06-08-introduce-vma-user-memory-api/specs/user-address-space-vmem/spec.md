## ADDED Requirements

### Requirement: 用户映射遵守 VMA 策略

BigOS SHALL ensure user page mappings created after VMA introduction are authorized by a compatible VMA before they are published in a user address-space root. VMA policy and page-table state MUST remain consistent for user code, data, heap, anonymous mappings, and stack pages.

#### Scenario: map user page checks VMA first

- **WHEN** kernel code maps a user page for exec, `brk`, anonymous mapping, user stack, or stack growth
- **THEN** BigOS MUST confirm the target virtual page is covered by a VMA with compatible permissions and ownership
- **AND** the resulting PTE user/writable/NX attributes MUST not grant access wider than the VMA permits

#### Scenario: mapping without VMA is rejected

- **WHEN** a user page mapping request targets a user low-half address that is not covered by a compatible VMA
- **THEN** BigOS MUST reject the mapping or enter a deterministic failure path before publishing a present PTE
- **AND** any physical page or intermediate page-table page allocated for the failed operation MUST be rolled back or left only in a documented fatal state

### Requirement: VMA 与页表范围校验职责分离

BigOS SHALL treat VMA lookup as the source of user virtual-memory policy and page-table probing as the source of currently materialized translation state. User range validation MUST consult both layers when kernel code will access user memory.

#### Scenario: VMA allows range but page missing

- **WHEN** a user range is covered by a compatible VMA but one or more pages are not currently present
- **THEN** BigOS MUST only recover if the range is an explicitly supported stack-growth fault or later documented demand-paging case
- **AND** otherwise it MUST return a deterministic error or terminate the current user process through the user fault path

#### Scenario: page present but VMA disallows access

- **WHEN** a page-table entry is present but the containing VMA is absent or lacks the requested read, write, or execute permission
- **THEN** BigOS MUST treat the user access as invalid
- **AND** it MUST NOT rely on the present PTE alone to authorize syscall user-buffer access
