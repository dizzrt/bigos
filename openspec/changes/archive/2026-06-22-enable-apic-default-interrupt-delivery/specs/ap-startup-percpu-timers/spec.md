## MODIFIED Requirements

### Requirement: LAPIC 与 IOAPIC 初始化边界
BigOS SHALL initialize the LAPIC boundary required for AP startup, local APIC EOI, per-CPU local timer operation, and APIC-backed default interrupt delivery. BigOS SHALL initialize the IOAPIC routing boundary required for supported default external IRQ sources, including scheduler timer ownership through local APIC timers and keyboard/input IRQ delivery through IOAPIC redirection when available.

#### Scenario: LAPIC 支持 AP startup
- **WHEN** BigOS enables AP startup on x86_64 hardware or emulator configuration with local APIC support
- **THEN** it MUST initialize the bootstrap LAPIC enough to send INIT/SIPI startup IPIs to discovered APs
- **AND** startup MUST fail deterministically when LAPIC support is unavailable or disabled

#### Scenario: LAPIC EOI 边界明确
- **WHEN** a LAPIC-backed interrupt is dispatched
- **THEN** BigOS MUST route EOI through the LAPIC-owned boundary for that interrupt source
- **AND** it MUST NOT confuse LAPIC EOI ownership with the legacy i8259 EOI path or the syscall vector path

#### Scenario: timer interrupt 迁移到 APIC-backed 路径
- **WHEN** BigOS enables the AP startup/per-CPU timer baseline
- **THEN** it MUST migrate the scheduler timer interrupt ownership from the legacy PIT/PIC tick path to the APIC-backed timer boundary
- **AND** PIT MAY remain available as a calibration reference but MUST NOT be the runtime scheduler tick owner for the APIC timer baseline

#### Scenario: supported external IRQ 迁移到 APIC-backed 默认路径
- **WHEN** LAPIC and IOAPIC initialization are valid and APIC-backed default interrupt delivery is enabled
- **THEN** BigOS MUST route supported default external IRQ sources through APIC-owned delivery rather than the legacy i8259 PIC path
- **AND** the active APIC path MUST include deterministic redirection, masking, target CPU selection, and LAPIC EOI ownership

#### Scenario: legacy PIC fallback 受控
- **WHEN** APIC-backed default interrupt delivery cannot be established
- **THEN** BigOS MAY continue in a documented BSP-only PIC/PIT fallback path or fail closed with diagnostics
- **AND** it MUST NOT claim APIC-backed default interrupt delivery, multi-core default IRQ routing, or APIC runtime parity for that fallback path

## ADDED Requirements

### Requirement: APIC default delivery preserves per-CPU timer assumptions
BigOS SHALL preserve the per-CPU timer and AP startup assumptions while enabling APIC-backed default external interrupt delivery.

#### Scenario: AP local timer remains CPU-local
- **WHEN** an online AP receives a local timer interrupt under APIC-backed default delivery
- **THEN** the tick MUST update CPU-local timer and scheduler state only through that CPU's initialized per-CPU boundaries
- **AND** it MUST NOT mutate bootstrap-only scheduler or timer state

#### Scenario: APIC routing does not change trampoline assumptions
- **WHEN** BigOS enables APIC-backed default interrupt delivery
- **THEN** it MUST NOT silently change the fixed low-address AP trampoline reservation, kernel page-table root assumptions, GDT/TSS setup, or AP online acknowledgement contract
- **AND** any required layout or handoff change MUST be reviewed separately
