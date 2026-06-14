## ADDED Requirements

### Requirement: VM/user-entry 边界作为真实架构消费点

BigOS architecture boundary discipline SHALL treat VM policy, address-space activation, user-entry mechanics, and fault handling as real core/architecture consumption points. Core code MAY consume semantic helpers for those concepts, but MUST NOT scatter x86_64-private CR3, descriptor, interrupt-frame, `iretq`, or raw page-table encoding details across unrelated core subsystems.

#### Scenario: 核心层消费 VM/user-entry 语义

- **WHEN** `kernel/core` or public kernel headers route process execution, user entry, address-space activation, user fault handling, or user-memory validation
- **THEN** the code MUST express the needed kernel concept through stable semantic boundaries or documented low-level entry interfaces
- **AND** unrelated core subsystems MUST NOT open-code x86_64 CR3 writes, GDT/TSS state, raw `iretq` frame layout, interrupt-frame offsets, or page-table bit encodings as portable policy

#### Scenario: 架构私有 VM/user-entry 细节留在 backend

- **WHEN** user entry or address-space activation requires CR3 writes, TLB invalidation semantics, GDT/TSS/RSP0 state, segment selectors, assembly frame construction, or x86_64 page-table bit encoding
- **THEN** those details MUST remain in architecture-specific implementation or explicitly named low-level entry code
- **AND** the core-facing contract MUST remain the stable VM/user-entry/fault semantic behavior rather than the raw x86_64 mechanism

#### Scenario: 边界整理不扩大 backend 承诺

- **WHEN** VM/user-entry architecture boundary cleanup is implemented, documented, or validated
- **THEN** the default runnable backend MUST remain the current x86_64 Legacy BIOS/MBR/exFAT path
- **AND** documentation MUST NOT claim runnable multi-architecture support, UEFI runtime parity, SMP support, broad file-backed `mmap`, dynamic linking, or complete POSIX process compatibility as a result of this cleanup
