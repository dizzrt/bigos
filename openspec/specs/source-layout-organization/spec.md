## Purpose

Define the required repository source layout, public header boundaries, tooling paths, and validation guarantees for BigOS implementation source organization.

## Requirements

### Requirement: Kernel implementation sources live under kernel

BigOS SHALL keep concrete kernel implementation sources for kernel runtime, architecture-specific boot code, memory management, device drivers, and startup runtime objects under top-level `kernel/`, while keeping freestanding user programs under top-level `user/` and the freestanding C++ support library under top-level `cpp/`.

#### Scenario: Developer inspects implementation layout

- **WHEN** a developer lists the repository root after the kernel source-root migration
- **THEN** concrete kernel implementation directories for `core`, `mm`, `drivers`, `arch`, and runtime startup objects are discoverable under `kernel/`
- **AND** freestanding user runtime, libc, init, shell, bins, and smoke programs remain discoverable under top-level `user/`
- **AND** C++ support implementation and headers remain discoverable under top-level `cpp/`
- **AND** the repository root no longer contains `src/` as the active implementation source root

#### Scenario: Developer inspects kernel core layout

- **WHEN** a developer lists `kernel/core`
- **THEN** kernel entry and core services are discoverable through subsystem directories for IRQ, timer, terminal, scheduler, syscall, process, filesystem, IPC, signal, time, low-level BigOS helpers, and string support
- **AND** `kernel/arch`, `kernel/drivers`, `kernel/mm`, and `kernel/runtime` remain siblings of `kernel/core` rather than being flattened into one mixed abstraction layer

#### Scenario: Build system collects implementation sources

- **WHEN** the primary build configuration is evaluated
- **THEN** it collects kernel core, memory-management, driver, architecture, and runtime startup sources from the `kernel/` paths
- **AND** it collects C++ support sources from the preserved top-level `cpp/` paths
- **AND** it does not rely on stale `src/` implementation source paths

### Requirement: Primary build owns boot artifact generation

BigOS SHALL build x86 Legacy BIOS boot-stage artifacts through the primary xmake build configuration rather than a subsystem-local Makefile.

#### Scenario: Developer inspects boot build ownership

- **WHEN** a developer checks how `kernel/arch/x86/boot` artifacts are produced
- **THEN** the build rules for MBR, DBR, extended DBR, and `boot.bin` MUST be discoverable from the primary xmake build configuration or xmake-included build files
- **AND** the boot artifact build MUST NOT require `kernel/arch/x86/boot/Makefile`
- **AND** the boot artifact build MUST NOT rely on stale former boot source-root paths

#### Scenario: Boot artifact outputs remain compatible

- **WHEN** xmake builds the Legacy BIOS boot-stage artifacts
- **THEN** it MUST produce `mbr.bin`, `dbr.bin`, `exdbr.bin`, and `boot.bin` under the documented build output area
- **AND** those artifacts MUST preserve the existing entry addresses, binary output format, object ordering assumptions, and size limits used by the current boot flow

#### Scenario: Source tree no longer exposes Makefile debug wrappers

- **WHEN** a developer lists project-level build and debug entry files after the migration
- **THEN** the repository MUST NOT present root `Makefile`, former boot source-root `Makefile`, or `kernel/arch/x86/boot/Makefile` as active build/debug entry points
- **AND** active documentation MUST point developers to xmake commands instead

### Requirement: Public headers remain separate from implementation sources

BigOS SHALL keep public kernel headers, freestanding C header subsets, and C++ support library headers in documented include roots so that include semantics are not tied to concrete implementation paths.

#### Scenario: Existing public include style remains valid

- **WHEN** source files include public headers such as `<bigos/io.h>`, `<bigos/memory.h>`, `<irq/interrupt.h>`, `<arch/x86/boot/boot_info.h>`, `<ktl/list.h>`, `<drivers/video/vga.h>`, or `<drivers/irqchip/i8259.h>`
- **THEN** the configured include search paths resolve those headers without requiring source files to include `kernel/` in public include directives
- **AND** KTL, `bits`, `ext`, and libsupc++ public or semi-public C++ support headers are resolved from documented `cpp/` include roots

#### Scenario: Public API boundary is reviewed

- **WHEN** implementation files are moved under `kernel/`
- **THEN** headers that remain public are available through documented include roots
- **AND** C++ support headers remain under `cpp/include` or `cpp/libsupc++/include` instead of being folded into the top-level kernel `include/`
- **AND** private implementation headers are either kept with their subsystem or explicitly documented as implementation-only include roots

### Requirement: Non-source project assets stay at semantic top-level locations

BigOS SHALL keep project assets that are not concrete implementation sources at top-level semantic locations unless a specific asset has a subsystem-local reason to move.

#### Scenario: Developer locates project entry assets

- **WHEN** a developer opens the repository root after the migration
- **THEN** build entry files, linker script, tools, documentation, tests, OpenSpec files, repository automation, and project metadata remain discoverable from top-level semantic paths
- **AND** `kernel/` contains kernel implementation source organization rather than becoming a catch-all project directory
- **AND** top-level `user/` remains the freestanding userland source root rather than moving under `kernel/`

#### Scenario: Xmake entry and included build files are organized

- **WHEN** a developer opens the repository root after the migration
- **THEN** root `xmake.lua` remains the single stable xmake entry file
- **AND** split xmake include files are discoverable under top-level `xmake/`
- **AND** the cross-toolchain definition formerly kept in root `toolchains.lua` is discoverable under top-level `xmake/`
- **AND** the repository root does not expose additional xmake entry files beside root `xmake.lua`

#### Scenario: Boot install helper lives with developer tools

- **WHEN** the boot disk-image install helper references boot artifacts after the migration
- **THEN** it is discoverable under top-level `tools/`
- **AND** its documentation or path handling continues to identify the related `kernel/arch/x86/boot` inputs and disk-image assumptions
- **AND** it does not rely on stale former boot source-root paths

### Requirement: Runtime behavior remains unchanged

The kernel source-root migration SHALL NOT change BigOS boot protocol behavior, linker address assumptions, startup object ordering, interrupt vector semantics, hardware access behavior, memory-management initialization behavior, userland packaging semantics, or syscall/user ABI behavior.

#### Scenario: Kernel build output remains bootloader-compatible

- **WHEN** the kernel is built after the kernel source-root migration
- **THEN** the produced kernel image remains named and located as expected by the existing boot/install flow
- **AND** the bootloader still loads the ELF64 kernel using the existing disk image and file-name assumptions

#### Scenario: Linker and ABI assumptions are preserved

- **WHEN** the migrated build links the kernel
- **THEN** the higher-half kernel link address remains `0xffffffff80000000`
- **AND** startup object ordering and linker-script section collection semantics remain equivalent to the pre-migration build
- **AND** IDT, syscall vector, page-table, direct-map, CR3-switching, and user address-space ABI assumptions remain unchanged

#### Scenario: Low-level subsystem semantics are preserved

- **WHEN** migrated kernel core, IRQ, driver, memory-management, KTL, boot, runtime, and userland package sources compile from their new paths
- **THEN** namespace names, public API names, include forms, interrupt vector choices, allocation initialization order, hardware IO ordering, and userland payload paths remain equivalent to their pre-migration behavior

### Requirement: Documentation and developer tooling reflect the new layout

BigOS SHALL update active documentation, roadmap-level structure language, archived OpenSpec path references, OpenSpec project context, editor configuration, tests, and developer tooling paths to describe and use the new kernel source-root layout.

#### Scenario: Documentation describes migrated layout

- **WHEN** a developer reads the active README, project guide, OpenSpec project context, or architecture documentation after the migration
- **THEN** the documented repository structure points to `kernel/arch`, `kernel/core`, `kernel/drivers`, `kernel/mm`, and `kernel/runtime`
- **AND** the documentation distinguishes kernel implementation source paths from public headers, top-level userland sources, tests, docs, tooling, and build entry files
- **AND** `roadmap.md` only describes the project-level kernel/user boundary without adding concrete entry points, commands, validation markers, source-level implementation details, or archive/version indexes

#### Scenario: Archived OpenSpec paths are refreshed

- **WHEN** a developer searches archived OpenSpec changes for subsystem paths after the migration
- **THEN** old active source path references are updated to the new layout
- **AND** archive files do not retain stale former active source-root path references
- **AND** historical task status, validation results, and design conclusions remain semantically unchanged

#### Scenario: Editor and static-analysis configuration use migrated paths

- **WHEN** clangd or equivalent editor diagnostics are configured after the migration
- **THEN** include paths and source-root assumptions reference the migrated `kernel/` layout
- **AND** preserved `cpp/` include paths are documented as active C++ support include roots rather than stale compatibility paths

### Requirement: Migration is validated by build and targeted checks

BigOS SHALL validate the kernel source-root migration with the narrowest useful checks for affected code paths and record unavailable checks with reasons and residual risk.

#### Scenario: Primary build validation is attempted

- **WHEN** the kernel source-root migration is complete
- **THEN** the primary `xmake` kernel build is run with the expected cross toolchain
- **AND** any inability to run the build is recorded with the missing tool, host limitation, and remaining risk

#### Scenario: Boot-sensitive paths are validated

- **WHEN** boot code, startup runtime objects, linker inputs, disk-image helper paths, or boot artifact build paths are migrated
- **THEN** bootability-sensitive assumptions are reviewed
- **AND** a QEMU headless smoke test is run when QEMU and the expected disk image/toolchain environment are available
- **AND** Bochs review or Bochs/QEMU cross-validation is run when Bochs and host-specific display/ROM configuration are available
- **AND** unavailable emulator validation is recorded with the remaining bootability risk

#### Scenario: Auxiliary checks cover changed language tooling

- **WHEN** C++ source/include paths or C++ build configuration change
- **THEN** clang or clangd auxiliary checks are run when practical using freestanding C++17, x86_64 target, no exceptions, no RTTI, and project include paths
- **AND** diagnostics are classified as historical, introduced by the migration, or toolchain/freestanding false positives

#### Scenario: Python helper migration is validated

- **WHEN** Python helper files or Python path configuration are changed as part of the kernel source-root migration
- **THEN** `uv run ruff check`, `uv run ruff format --check`, `uv run pyright`, and `uv run pytest` are run when Python tooling is available
- **AND** unavailable Python validation is recorded with reasons and remaining risk
