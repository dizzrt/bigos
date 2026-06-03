## ADDED Requirements

### Requirement: Runtime memory validation preserves allocator invariants

Early memory correctness validation SHALL include runtime checks that allocator operations restore observable accounting and do not leave leaked kernel virtual ranges or physical page allocations after self-test completion.

#### Scenario: Self-test restores physical free page count
- **WHEN** the memory runtime self-test allocates and releases physical pages through tested paths
- **THEN** `g_nr_free_pages()` after the test equals the value recorded before the tested allocations, excluding allocations intentionally kept by the kernel

#### Scenario: Self-test does not require later kernel subsystems
- **WHEN** memory runtime self-test executes
- **THEN** it completes without requiring scheduler, IRQ enable, SMP services, filesystem services, user mode, or hosted runtime APIs

#### Scenario: Runtime validation complements source-level tests
- **WHEN** memory runtime validation is added
- **THEN** existing source-level tests, build checks, and OpenSpec validation remain part of the verification record rather than being replaced by boot-only smoke
