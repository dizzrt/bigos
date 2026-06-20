# Architecture Core Boundaries

BigOS currently has one default runtime-parity architecture/backend path:
x86_64 through the Legacy BIOS/MBR/exFAT boot flow. A runnable x86_64 UEFI boot
backend spike exists, but it is not the default runtime-parity backend.
Architecture boundary work keeps the Legacy BIOS path runnable and makes the
split between kernel core concepts and x86_64 mechanisms explicit. It does not
add UEFI runtime parity, a non-x86 backend, SMP, a broad device model, dynamic
linking, or complete POSIX coverage.

## Boundary Rule

Core code should name the concept it consumes, while x86_64 backend or device
code should own the mechanism that implements it.

- Boot handoff consumers may accept a normalized boot-info pointer, but fixed
  Legacy BIOS addresses, E820 layout details, loader page-table reservations,
  and boot-sector mechanics stay in the x86 boot path.
- Interrupt and syscall consumers may route exceptions, IRQs, and `int 0x80`
  dispatch through the current interrupt APIs, but IDT descriptors, vector
  table installation, CR2 reads, saved register frame layout, and EOI rules
  remain x86_64/i8259 implementation details.
- Scheduler and process code may save a scheduler stack pointer, request address
  space activation, enter/resume user context, and consume the narrow
  `bigos::arch_context` semantic boundary, but AMD64 callee-saved switch frames,
  `iretq` entry frames, GDT selectors, and TSS `rsp0` details stay in the
  x86_64 implementation.
- VM/user-entry consumers may use the narrow
  `include/bigos/arch_vm_user_boundary.h` boundary for active-root queries,
  address-space activation, user-return classification, user resume-frame
  capture, and ring3 entry. CR3 masking/writes, TLB effects, user selectors,
  TSS state, and `iretq` frame details stay in architecture-owned code.
- Memory-management code may expose page-count kernel allocation, user-root
  operations, and address-space activation, but 4-level PML4 layout, recursive
  self-mapping, direct-map constants, PTE bit positions, `invlpg`, and CR3
  instructions are x86_64 paging details.
- Device drivers own PC hardware constants for VGA text memory, COM1 serial,
  i8259 PIC, PIT, CMOS RTC, keyboard scancodes, and ATA PIO ports; core code
  should not duplicate those constants outside the narrow driver-facing path.

## Current Consumption Points

The current codebase still intentionally contains x86_64 coupling at real
runtime seams:

- `kernel/arch/x86/boot` owns BIOS boot sectors, long-mode transition, early page
  tables, ATA/exFAT loading, and the concrete producer side of boot-info data.
- `kernel/core/irq` owns the current IDT setup, ISR stubs, x86 exception state,
  IRQ dispatch, syscall vector dispatch, and i8259 EOI split.
- `kernel/core/sched` owns the single-core scheduler policy while the assembly
  context-switch frame remains an AMD64 ABI detail. Scheduler policy consumes
  IRQ-return context eligibility and kernel context switching through the
  `include/bigos/arch_context.h` boundary rather than open-coding raw frame
  offsets or the assembly symbol.
- `kernel/mm` owns current x86_64 page-table manipulation and CR3 activation
  because no alternate paging backend exists yet. Core process policy must still
  treat VMA/process metadata as authorization and page tables as materialized
  state.
- `kernel/core/proc` owns process lifecycle, VMA policy, safe teardown, and the
  bounded user-fault lifecycle. It consumes address-space activation, TSS
  `rsp0`, user entry, user-return classification, and fork resume-frame capture
  through `include/bigos/arch_vm_user_boundary.h`; the lower-level
  `include/bigos/user_mode.h` and entry assembly remain x86_64 implementation
  details.
- `kernel/drivers` and driver-facing core paths own the legacy PC device access
  for VGA, serial, PIC, PIT, CMOS RTC, keyboard, and ATA PIO.

These are current facts, not a promise that the kernel core is architecture
neutral today.

## Preserved Assumptions

Architecture-boundary cleanups must preserve the following assumptions unless a
separate change declares and validates the behavior change:

- Higher-half kernel base, Legacy BIOS fixed handoff addresses, boot-info ABI,
  and linker entry assumptions documented in `docs/en/arch/x86-boot-layout.md`.
- IDT vectors, exception versus IRQ versus syscall dispatch split, and the rule
  that the syscall path does not send an i8259 EOI.
- Interrupt frame and scheduler context-switch frame layouts consumed by fork,
  signal, syscall, IRQ-return preemption, and context switching.
- The `include/bigos/arch_context.h` boundary is a current x86_64 backend
  core-facing contract only. It does not promise a complete HAL, SMP, UEFI
  runtime parity, non-x86 runtime parity, APIC/IOAPIC support, or HPET support.
- x86_64 page-table layout, recursive self-mapping window, direct map window,
  CR3 root semantics, and TLB invalidation behavior.
- Existing higher-half kernel mappings shared into user roots, user low-half
  isolation, KVMEM/direct-map availability, user stack assumptions, and current
  CR3 switching semantics.
- Raw disk image layout, Legacy BIOS/MBR/exFAT boot packaging, and ATA PIO
  storage path used by the default runnable backend.
- Minimal syscall ABI: syscall number and arguments in the existing x86_64
  registers, result returned in `rax`, and bounded user-buffer validation.

## SMP Preparation Boundary

The current SMP-ready boundary includes AP startup, per-CPU local timer state,
and bounded per-CPU scheduler domains. CPU-local accessors describe current
thread, current process, active address-space root, IRQ-visible nesting,
preemption-disable depth, and pending reschedule state for each online CPU that
has been initialized as schedulable.

- Scheduler ownership is explicit per CPU: ready queues, wait queues, sleep
  lists, the idle thread, terminated list, and reschedule intent belong to the
  owning scheduler domain. Initial placement and wakeup may enqueue work on a
  remote online CPU through the scheduler boundary, publish runnable state first,
  then set reschedule pending and send a scheduler nudge.
- IRQ routing remains on the existing IDT/i8259/PIT/keyboard/syscall model.
  Exception and external IRQ paths are non-blocking; `int 0x80` keeps the
  existing syscall ABI and does not send an i8259 EOI. LAPIC timer and scheduler
  nudge interrupts use LAPIC EOI; the nudge is not generic IPI routing.
- TLB invalidation is expressed through a boundary that carries address-space
  root, virtual page or range, target CPU mask, and completion requirement. The
  single-core implementation accepts only the bootstrap CPU target and completes
  with local `invlpg` or CR3 reload.
- Shared scheduler state, IRQ-visible state, and page-table updates publish under
  interrupt-disabled sections or the selected local boundary before becoming
  visible to handlers, fault paths, or future remote CPUs.
- Generic IPI routing, TLB shootdown acknowledgements, CPU hotplug, NUMA, RCU,
  full APIC external interrupt migration, and broad load balancing remain future
  dependencies.

## Review Checklist

Use this checklist for changes touching `kernel/core`, `kernel/mm`, public
kernel headers, or x86_64 backend/device paths:

- Identify whether each dependency is a core concept, x86_64 backend mechanism,
  device-driver mechanism, or build/link constraint.
- Keep x86_64 descriptors, GDT/TSS/IDT details, CR2/CR3 instructions, raw port
  constants, and assembly frame layouts out of generic-looking interfaces unless
  the interface is explicitly the current x86_64 ABI boundary.
- Prefer opaque handoff pointers and semantic helper names when a core caller
  does not need concrete x86_64 structure fields.
- Avoid speculative HALs, empty non-x86 backend directories, or interfaces
  without a current caller and implementation.
- For IRQ, port I/O, MMIO, or driver state changes, review interrupt safety,
  reentrancy, hardware access ordering, and deterministic failure behavior.
- For allocator, page-table, or early-memory changes, review initialization
  phase, allocation context, object lifetime, alignment, rollback, and failure
  behavior.
- For VM/user-entry/fault changes, verify that VMA/process metadata authorizes
  user access before page-table materialization, recoverable CPL3 faults stay
  limited to implemented VMA-backed demand-zero/COW/stack-growth cases, and
  fault/exit/reaper paths never free the active user root, current kernel stack,
  or process object on an unsafe return path.

## Validation

Documentation-only or include-direction boundary changes should at least run
OpenSpec status checks and targeted consistency searches. Runtime refactors that
touch boot, IRQ, timer, scheduler context switch, memory mapping, syscall,
user-mode entry, or hardware drivers should also run the narrowest available
x86_64 cross-build. If local QEMU/Bochs and the cross toolchain are available,
prefer a QEMU headless smoke for runtime paths and consider Bochs or QEMU/Bochs
cross-validation for early boot, port I/O, or hardware-behavior risk.
