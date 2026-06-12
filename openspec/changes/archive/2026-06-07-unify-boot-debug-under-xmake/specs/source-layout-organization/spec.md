## ADDED Requirements

### Requirement: Primary build owns boot artifact generation

BigOS SHALL build x86 Legacy BIOS boot-stage artifacts through the primary xmake build configuration rather than a subsystem-local Makefile.

#### Scenario: Developer inspects boot build ownership

- **WHEN** a developer checks how `kernel/arch/x86/boot` artifacts are produced
- **THEN** the build rules for MBR, DBR, extended DBR, and `boot.bin` MUST be discoverable from the primary xmake build configuration or xmake-included build files
- **AND** the boot artifact build MUST NOT require `kernel/arch/x86/boot/Makefile`

#### Scenario: Boot artifact outputs remain compatible

- **WHEN** xmake builds the Legacy BIOS boot-stage artifacts
- **THEN** it MUST produce `mbr.bin`, `dbr.bin`, `exdbr.bin`, and `boot.bin` under the documented build output area
- **AND** those artifacts MUST preserve the existing entry addresses, binary output format, object ordering assumptions, and size limits used by the current boot flow

#### Scenario: Source tree no longer exposes Makefile debug wrappers

- **WHEN** a developer lists project-level build and debug entry files after the migration
- **THEN** the repository MUST NOT present root `Makefile` or `kernel/arch/x86/boot/Makefile` as active build/debug entry points
- **AND** active documentation MUST point developers to xmake commands instead
