## MODIFIED Requirements

### Requirement: Early memory runtime self-test is switchable

BigOS SHALL provide a switchable early memory runtime self-test that runs after `init_mem()` and before IRQ/PIC initialization or IRQ enabling when explicitly enabled for validation builds.

#### Scenario: Self-test is enabled

- **WHEN** the kernel is built with the memory self-test switch enabled and reaches the post-`init_mem()` point
- **THEN** the kernel runs memory self-test coverage before initializing IRQ/PIC or enabling maskable interrupts

#### Scenario: Self-test is disabled

- **WHEN** the kernel is built without the memory self-test switch
- **THEN** the existing boot path continues without executing memory self-test code
