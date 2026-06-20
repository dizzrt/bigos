## Purpose

Define the bounded x86_64 AP startup and per-CPU local timer baseline for BigOS, including CPU topology discovery, AP trampoline entry, LAPIC/IOAPIC activation boundaries, CPU-local timer state, and validation expectations without claiming complete SMP scheduling or full APIC external interrupt migration.

## Requirements

### Requirement: CPU 拓扑枚举与上线状态
BigOS SHALL maintain a bounded x86_64 CPU topology table derived first from the Legacy BIOS MP table, with ACPI MADT as a controlled fallback topology source when the MP table is unavailable or invalid. The table MUST identify the bootstrap processor, discovered application processors, APIC identifiers, IOAPIC descriptors needed by this change, per-CPU state ownership, startup state, and deterministic failure status before any AP is allowed to execute kernel work.

#### Scenario: BSP 记录自身 CPU 状态
- **WHEN** BigOS initializes CPU topology on the bootstrap processor
- **THEN** it MUST mark the bootstrap processor as online
- **AND** it MUST bind current CPU, current thread ownership, interrupt nesting, preemption state, and timer state to the bootstrap CPU slot

#### Scenario: AP 枚举受容量限制
- **WHEN** BigOS discovers more CPUs from the MP table or ACPI MADT than the configured bounded CPU table can represent
- **THEN** it MUST record only supported CPUs up to the bounded capacity
- **AND** it MUST leave unsupported CPUs offline with a deterministic diagnostic status rather than attempting uncontrolled startup

#### Scenario: MP table 缺失或无效
- **WHEN** BigOS cannot find a valid MP table or cannot extract the CPU/APIC topology required by AP startup
- **THEN** it MUST attempt ACPI MADT fallback topology parsing before declaring topology unavailable
- **AND** if MADT is unavailable or invalid it MUST fail closed or continue in a documented BSP-only diagnostic path
- **AND** it MUST NOT fall back to an undocumented emulator-only topology as the normal runtime source

#### Scenario: ACPI MADT fallback 受控
- **WHEN** BigOS uses ACPI MADT as the fallback topology source
- **THEN** it MUST parse only the local APIC and IOAPIC records required by AP startup and per-CPU timer routing
- **AND** it MUST NOT describe this as complete ACPI enumeration, PCI discovery, device power management, or backend parity

#### Scenario: APIC id 映射稳定
- **WHEN** BigOS records an application processor in the CPU table
- **THEN** it MUST store the hardware APIC id and internal CPU id mapping
- **AND** later AP startup acknowledgement MUST match the recorded APIC id before the CPU can become online

### Requirement: AP trampoline 与长模式进入
BigOS SHALL provide a controlled x86_64 AP startup path that prepares a low-address trampoline, AP startup parameters, AP stack, page-table root, GDT/TSS state, and long-mode entry target before sending AP startup IPIs.

#### Scenario: trampoline 低地址区域受保护
- **WHEN** BigOS copies or prepares the AP startup trampoline
- **THEN** the trampoline memory range MUST be explicitly reserved from normal allocator reuse
- **AND** the reservation MUST NOT silently change kernel link addresses, direct-map layout, page-table self-mapping, disk layout, or user ABI assumptions

#### Scenario: AP 进入长模式后绑定 per-CPU state
- **WHEN** an application processor reaches the kernel AP entry point after trampoline execution
- **THEN** it MUST load the kernel page-table context required by the current x86_64 runtime
- **AND** it MUST bind its CPU-local state, kernel stack, GDT/TSS ownership, IDT availability, and interrupt/preemption counters before reporting online

#### Scenario: AP startup 超时失败
- **WHEN** the bootstrap processor sends the startup sequence for an AP and the AP does not acknowledge online state within the bounded timeout
- **THEN** BigOS MUST keep that AP offline
- **AND** it MUST fail closed or continue in a documented BSP-only diagnostic path without exposing the AP to scheduler or timer consumers

### Requirement: LAPIC 与 IOAPIC 初始化边界
BigOS SHALL initialize the LAPIC boundary required for AP startup, local APIC EOI, and per-CPU local timer operation, and SHALL provide an IOAPIC initialization/routing boundary for the timer interrupt migration required by this change. BigOS MUST NOT require complete migration of all legacy external IRQ delivery in this change.

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

#### Scenario: 其他 legacy IRQ 不强制迁移
- **WHEN** IOAPIC initialization and timer routing are present for this baseline
- **THEN** BigOS MAY keep existing legacy external IRQ routing for sources not explicitly migrated by this change
- **AND** documentation or validation MUST NOT claim complete APIC-backed default interrupt delivery for all devices

### Requirement: per-CPU local timer 基线
BigOS SHALL initialize a local timer for each online CPU using PIT-referenced calibration for the first implementation and expose per-CPU tick state through the existing timer/scheduler context boundary. Before per-CPU run queues are enabled, AP ticks remain limited to CPU-local timer or idle validation; after per-CPU run queues are enabled, AP ticks MAY drive scheduling for work already assigned to that AP scheduler domain.

#### Scenario: BSP local timer 初始化
- **WHEN** the bootstrap CPU enables the APIC-backed timer baseline
- **THEN** BigOS MUST initialize a bounded local timer state for the bootstrap CPU
- **AND** it MUST calibrate or validate that timer against the PIT reference before publishing it as the runtime scheduler tick source

#### Scenario: PIT 校准失败
- **WHEN** the PIT-referenced LAPIC timer calibration fails or times out
- **THEN** BigOS MUST fail closed or continue in a documented BSP-only diagnostic path
- **AND** it MUST NOT publish an uncalibrated per-CPU timer as a valid scheduler tick source

#### Scenario: AP local timer 初始化
- **WHEN** an application processor reports online
- **THEN** BigOS MUST initialize that CPU's local timer state before allowing AP timer interrupts to be counted as valid
- **AND** AP local timer interrupts MUST update CPU-local tick state without entering unsupported filesystem, blocking I/O, or userland paths

#### Scenario: AP tick remains local before scheduler SMP
- **WHEN** an AP receives a local timer tick before per-CPU run queues and cross-CPU wakeups are implemented
- **THEN** BigOS MUST NOT migrate runnable work to or from that AP
- **AND** the tick MUST remain within the documented CPU-local timer or idle validation boundary

#### Scenario: AP tick drives local scheduling after scheduler SMP
- **WHEN** an AP receives a local timer tick after per-CPU run queues and cross-CPU wakeups are enabled
- **THEN** BigOS MAY account the AP's current ordinary thread and request preemption through that AP's scheduler domain
- **AND** it MUST NOT mutate the bootstrap CPU run queue, bootstrap current thread, or any other CPU's scheduler state except through documented cross-CPU scheduler boundaries

### Requirement: AP 启动验证边界
BigOS SHALL provide deterministic validation for AP startup and per-CPU timers using the current x86_64 toolchain and emulator-first workflow, while recording unavailable runtime validation explicitly.

#### Scenario: QEMU 多核 smoke 验证
- **WHEN** QEMU, the x86_64 cross toolchain, and the project helper scripts are available
- **THEN** validation MUST include a bounded multi-core boot smoke that observes AP online acknowledgement and per-CPU timer progress
- **AND** the smoke MUST also confirm that the bounded userland baseline is not broken by AP startup

#### Scenario: validation 不声称完整 SMP
- **WHEN** AP startup and per-CPU timer validation passes
- **THEN** validation notes MUST identify the result as AP bring-up and timer baseline coverage
- **AND** they MUST NOT claim per-CPU run queues, cross-CPU scheduling, IPI TLB shootdown, CPU hotplug, or complete APIC default interrupt delivery

#### Scenario: 缺失工具记录残余风险
- **WHEN** QEMU, Bochs, xmake, cross-binutils, or local emulator APIC configuration is unavailable
- **THEN** validation notes MUST record the skipped runtime checks, substitute checks, and residual AP startup or timer risk
- **AND** they MUST NOT claim emulator-validated multi-core behavior
