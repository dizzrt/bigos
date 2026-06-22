## ADDED Requirements

### Requirement: APIC-backed delivery is the x86_64 default external IRQ path
BigOS SHALL use APIC-backed delivery as the default x86_64 runtime path for supported external interrupts once LAPIC and IOAPIC initialization succeeds. The default path MUST identify the interrupt source, owning irqchip, delivery target, vector, mask state, and EOI owner before enabling the interrupt source.

#### Scenario: APIC path becomes active after initialization
- **WHEN** BigOS completes x86_64 interrupt initialization with valid APIC topology, enabled LAPIC state, and configured IOAPIC redirection entries
- **THEN** supported default external IRQ sources MUST be marked APIC-backed
- **AND** BigOS MUST NOT continue routing those active sources through the i8259 PIC path

#### Scenario: unsupported APIC initialization fails closed
- **WHEN** APIC topology, LAPIC enablement, or required IOAPIC routing cannot be established
- **THEN** BigOS MUST either enter a documented BSP-only PIC/PIT fallback path or fail closed with deterministic diagnostics
- **AND** it MUST NOT expose a partially APIC-backed multi-core runtime as the default interrupt model

#### Scenario: interrupt target is schedulable and initialized
- **WHEN** BigOS routes a supported external IRQ through IOAPIC redirection
- **THEN** the selected target CPU MUST be online and have initialized per-CPU interrupt state
- **AND** unsupported, offline, failed, or uninitialized CPUs MUST NOT be selected as delivery targets

### Requirement: EOI ownership is unambiguous
BigOS SHALL send end-of-interrupt notifications only through the irqchip owner for the delivered interrupt. CPU exceptions and the syscall vector MUST NOT send irqchip EOI; legacy fallback IRQs MUST use PIC EOI; APIC-backed external IRQs, local timer interrupts, and IPIs MUST use LAPIC EOI.

#### Scenario: APIC-backed external IRQ sends LAPIC EOI
- **WHEN** an APIC-backed external IRQ completes its registered handler
- **THEN** BigOS MUST send exactly one EOI through the LAPIC-owned boundary
- **AND** it MUST NOT send i8259 EOI for that interrupt

#### Scenario: PIC fallback IRQ sends PIC EOI
- **WHEN** BigOS runs in the documented PIC fallback path and a remapped PIC IRQ completes
- **THEN** BigOS MUST send the required PIC EOI for that legacy IRQ
- **AND** it MUST NOT also send LAPIC EOI for the fallback IRQ

#### Scenario: exceptions and syscalls do not send irqchip EOI
- **WHEN** a CPU exception vector or the `int 0x80` syscall vector reaches the dispatch layer
- **THEN** BigOS MUST handle it outside external irqchip EOI ownership
- **AND** it MUST NOT send PIC or LAPIC EOI for that path

### Requirement: default timer and keyboard IRQ sources use APIC delivery
BigOS SHALL migrate supported default timer and keyboard/input interrupt sources to APIC-backed delivery when the APIC default path is active, while preserving existing handler contracts and bounded userland behavior.

#### Scenario: scheduler tick uses per-CPU LAPIC timer
- **WHEN** APIC-backed default interrupt delivery is active
- **THEN** the scheduler tick MUST be delivered through the per-CPU LAPIC timer path
- **AND** PIT MAY remain available only for calibration, diagnostics, or documented fallback

#### Scenario: keyboard input uses IOAPIC routing
- **WHEN** APIC-backed default interrupt delivery is active and keyboard/input IRQ is enabled
- **THEN** BigOS MUST route the keyboard/input IRQ through a configured IOAPIC redirection entry to the initialized online BSP as the first default target
- **AND** the keyboard handler MUST preserve its bounded input handoff and handler-does-not-EOI contract
- **AND** the target selection MUST remain behind an internal boundary that can later support a service CPU or IRQ affinity without changing the IOAPIC routing contract

#### Scenario: userland baseline remains reachable
- **WHEN** APIC-backed timer and keyboard delivery are active
- **THEN** the resident init and shell baseline MUST remain reachable under the bounded userland contract
- **AND** validation MUST NOT require dynamic linking, broad POSIX job control, or complete device enumeration

### Requirement: user-visible ABI review is recorded
BigOS SHALL explicitly review user-visible ABI and behavior boundaries affected by APIC-backed default interrupt delivery. The review MUST cover syscall entry, process/signal behavior, time/timer observations, boot handoff assumptions, memory and disk layout assumptions, and public diagnostic markers.

#### Scenario: syscall ABI remains stable
- **WHEN** APIC-backed default interrupt delivery is enabled
- **THEN** vector `0x80`, syscall argument passing, syscall return behavior, and syscall non-EOI semantics MUST remain unchanged
- **AND** any intentional syscall ABI change MUST be marked as breaking before implementation completes

#### Scenario: layout assumptions remain stable
- **WHEN** APIC-backed default interrupt delivery is enabled
- **THEN** BigOS MUST NOT silently change kernel link addresses, AP trampoline range, page-table layout, direct-map layout, disk image layout, or user address-space layout
- **AND** any required layout change MUST be reviewed as a separate ABI/layout decision

#### Scenario: validation notes include ABI conclusion
- **WHEN** implementation validation is recorded
- **THEN** the notes MUST state whether user-visible ABI changed
- **AND** if no ABI changed, the notes MUST identify the reviewed boundaries rather than relying only on source diffs

### Requirement: APIC default delivery validation is reproducible
BigOS SHALL validate APIC-backed default interrupt delivery with deterministic source checks and bounded emulator smoke when local tooling supports it.

#### Scenario: source checks cover routing invariants
- **WHEN** this change is implemented
- **THEN** validation MUST include source-level checks for vector classification, irqchip ownership, EOI uniqueness, APIC fallback gating, and absence of blocking operations in hard IRQ paths

#### Scenario: QEMU multi-core smoke covers default delivery
- **WHEN** QEMU, xmake, the x86_64 cross toolchain, and APIC-capable local configuration are available
- **THEN** validation MUST include a bounded multi-core smoke that observes APIC-backed timer progress, supported external IRQ delivery, and the bounded userland baseline
- **AND** it MUST not claim validation for CPU hotplug, NUMA, MSI/MSI-X, complete IRQ affinity, or non-x86_64 backends

#### Scenario: missing tooling is explicit
- **WHEN** QEMU, Bochs, cross-binutils, APIC support, display/ROM configuration, or helper script support is unavailable
- **THEN** validation notes MUST record skipped runtime checks, substitute checks, and residual interrupt-delivery risk
- **AND** they MUST NOT claim emulator-validated APIC default delivery for skipped environments
