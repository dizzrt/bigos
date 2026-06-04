## Purpose

Define switchable early runtime validation for BigOS memory management, including allocator smoke coverage, deterministic boot markers, and bounded emulator validation.

## Requirements

### Requirement: Early memory runtime self-test is switchable

BigOS SHALL provide a switchable early memory runtime self-test that runs after `init_mem()` and before IRQ/PIC initialization or IRQ enabling when explicitly enabled for validation builds.

#### Scenario: Self-test is enabled

- **WHEN** the kernel is built with the memory self-test switch enabled and reaches the post-`init_mem()` point
- **THEN** the kernel runs memory self-test coverage before initializing IRQ/PIC or enabling maskable interrupts

#### Scenario: Self-test is disabled

- **WHEN** the kernel is built without the memory self-test switch
- **THEN** the existing boot path continues without executing memory self-test code

### Requirement: Runtime self-test covers core allocator paths

The memory runtime self-test SHALL cover successful allocation and release through slab small objects, kernel virtual pages, and physical buddy order allocation in the current single-core early boot environment.

#### Scenario: Small object allocation smoke

- **WHEN** memory self-test runs after `init_mem()`
- **THEN** it allocates and frees representative `kmalloc()` sizes across existing static size classes and verifies each successful object is writable

#### Scenario: Kernel virtual page smoke

- **WHEN** memory self-test runs after `init_mem()`
- **THEN** it allocates and frees one-page and multi-page kernel virtual ranges and verifies mapped ranges are writable before release

#### Scenario: Physical order smoke

- **WHEN** memory self-test directly exercises internal physical order allocation
- **THEN** it allocates and frees representative low orders and verifies global free-page accounting is restored after release

### Requirement: Runtime self-test reports deterministic boot markers

The validation build SHALL emit deterministic success and failure markers that emulator smoke tooling can observe without requiring hosted OS services.

#### Scenario: Self-test success marker

- **WHEN** memory self-test completes all checks successfully
- **THEN** the kernel emits a fixed success marker before continuing to the normal kernel reached output

#### Scenario: Self-test failure marker

- **WHEN** memory self-test detects an allocation, mapping, release, or accounting invariant failure
- **THEN** the kernel emits a fixed failure marker identifying the failing stage and halts safely

### Requirement: Emulator runtime smoke is bounded and reproducible

Memory runtime validation SHALL include a bounded emulator smoke path when Bochs and boot assets are available, and SHALL record explicit reasons when runtime smoke cannot run locally.

#### Scenario: Bochs smoke observes memory marker

- **WHEN** Bochs, ROM paths, generated disk image, and smoke oracle are available
- **THEN** the validation command boots the kernel with memory self-test enabled and verifies the fixed success marker appears within a bounded time

#### Scenario: Runtime smoke unavailable

- **WHEN** Bochs runtime smoke cannot run because the local emulator, ROM, or oracle is unavailable
- **THEN** validation records the missing dependency, the alternative checks that passed, and the remaining bootability risk
