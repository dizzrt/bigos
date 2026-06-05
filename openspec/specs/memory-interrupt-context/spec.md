## Purpose

Define BigOS early-kernel memory and interrupt-context contracts, including allocator API context safety, single-core interrupt guarding for allocator metadata, IRQ handler allocation restrictions, and reproducible validation expectations.

## Requirements

### Requirement: Memory APIs declare interrupt-context contracts

BigOS SHALL declare context-safety contracts for early memory management APIs so callers can distinguish IRQ-handler-safe operations from ordinary non-interrupt-context allocator operations.

#### Scenario: Public allocator entry points are not IRQ-handler safe

- **WHEN** kernel code uses `kmalloc()`, `free()`, `alloc_kernel_pages()`, `free_pages()`, or global `operator new`
- **THEN** the corresponding public declarations or adjacent documentation MUST state that these ordinary allocator entry points are not callable from IRQ handlers in the current single-core early kernel

#### Scenario: Read-only accounting contract is explicit

- **WHEN** memory code exposes read-only accounting or diagnostic helpers
- **THEN** each helper MUST document whether it is context-agnostic, IRQ-disabled-only, or non-interrupt-context-only

#### Scenario: IRQ producers use preallocated storage

- **WHEN** an IRQ handler needs to hand off data to later non-interrupt-context code
- **THEN** the handoff path MUST use static or boot-time-prepared bounded storage rather than calling ordinary allocator APIs from the handler

### Requirement: Interrupt guard preserves caller interrupt state

BigOS SHALL provide a minimal single-core interrupt guard or equivalent critical-section primitive that prevents maskable IRQ interleaving while preserving the caller's previous IF state.

#### Scenario: Guard restores enabled state

- **WHEN** code enters the guard while maskable interrupts are enabled
- **THEN** the guard MUST disable maskable interrupts for the protected region and restore them when leaving the outer protected region

#### Scenario: Guard preserves disabled state

- **WHEN** code enters the guard while maskable interrupts are already disabled
- **THEN** the guard MUST leave maskable interrupts disabled after the protected region exits

#### Scenario: Guard remains single-core only

- **WHEN** code or documentation describes the interrupt guard
- **THEN** it MUST state that the guard protects only same-CPU maskable IRQ interleaving and does not provide SMP mutual exclusion, NMI protection, blocking semantics, or scheduler locking

### Requirement: Allocator metadata updates use bounded critical sections

BigOS SHALL protect allocator metadata updates that can be observed by IRQ-enabled code with bounded single-core critical sections while keeping long-running or blocking-like work outside those sections.

#### Scenario: Buddy metadata update is not interrupt-interleaved

- **WHEN** buddy allocation, split, free, merge, or accounting code mutates free lists, allocated lists, `PageBlock` ownership, or global free-page counts after IRQs may be enabled
- **THEN** the mutation MUST occur under the interrupt guard or an equivalent documented IRQ-disabled boundary

#### Scenario: Slab metadata update is not interrupt-interleaved

- **WHEN** slab or kmalloc code mutates cache lists, slab object bitmaps, allocation-kind metadata, large-allocation records, or object accounting after IRQs may be enabled
- **THEN** the mutation MUST occur under the interrupt guard or an equivalent documented IRQ-disabled boundary

#### Scenario: VMem metadata update is not interrupt-interleaved

- **WHEN** kernel virtual memory code mutates free/used virtual range lists, backing records, page-table descriptors, or TLB invalidation bookkeeping after IRQs may be enabled
- **THEN** the mutation MUST occur under the interrupt guard or an equivalent documented IRQ-disabled boundary

#### Scenario: Critical sections remain bounded

- **WHEN** allocator code enters a guarded critical section
- **THEN** the guarded region MUST NOT include `mdelay()`, unbounded waiting loops, console/serial bulk output, filesystem operations, scheduler operations, or user-mode interactions

### Requirement: Interrupt handlers avoid ordinary allocator APIs

BigOS SHALL keep CPU exception handlers and external IRQ handlers free from ordinary dynamic allocation and kernel virtual page allocation until a later change explicitly introduces an IRQ-safe allocator.

#### Scenario: Timer IRQ avoids allocator calls

- **WHEN** the timer IRQ0 handler runs
- **THEN** it MUST NOT call `kmalloc()`, `free()`, `alloc_kernel_pages()`, `free_pages()`, global `new`, or global `delete`

#### Scenario: Keyboard IRQ avoids allocator calls

- **WHEN** the keyboard IRQ1 handler runs
- **THEN** it MUST NOT call `kmalloc()`, `free()`, `alloc_kernel_pages()`, `free_pages()`, global `new`, or global `delete`

#### Scenario: Page fault diagnostic path avoids allocator calls

- **WHEN** the diagnostic-only `#PF` handler runs
- **THEN** it MUST NOT allocate memory, free memory, modify page tables for recovery, or retry the faulting instruction

### Requirement: Memory interrupt-context validation is reproducible

BigOS SHALL validate the memory interrupt-context contract with source-level checks, the existing memory runtime self-test path, build checks, and OpenSpec validation.

#### Scenario: Source checks cover contracts

- **WHEN** this change is implemented
- **THEN** tests MUST verify API context annotations, guard IF save/restore behavior, ISR allocator-call exclusions, and `mm_self_test()` ordering before IRQ enable

#### Scenario: Runtime self-test remains early

- **WHEN** `BIGOS_MM_SELF_TEST` is enabled
- **THEN** `mm_self_test()` MUST continue to run after `init_mem()` and before PIC initialization, IRQ unmasking, and `sti`

#### Scenario: Build and OpenSpec validation are recorded

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest useful build or static syntax check, relevant `uv run pytest` commands, and `openspec validate prepare-memory-for-interrupt-context --strict`

#### Scenario: Emulator limitations are explicit

- **WHEN** Bochs runtime smoke cannot be completed because emulator, ROM, serial oracle, image lock, or interactive input support is unavailable
- **THEN** validation MUST record the unavailable dependency, the alternative checks that passed, and the remaining bootability risk
