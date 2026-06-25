## MODIFIED Requirements

### Requirement: Non-source project assets stay at semantic top-level locations

BigOS SHALL keep project assets that are not concrete implementation sources at top-level semantic locations unless a specific asset has a subsystem-local reason to move. Developer Python tooling SHALL live under top-level `tools/`, and the active BigOS developer tooling entry SHALL be the `tools/bigosdev/` executable package rather than standalone legacy helper scripts.

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

#### Scenario: BigOS developer tooling lives under tools

- **WHEN** the Python developer tooling references boot artifacts, emulator configuration, runtime smoke cases, or generated disk images after the migration
- **THEN** it MUST be discoverable under top-level `tools/bigosdev/`
- **AND** its documentation or path handling MUST identify the related `kernel/arch/x86/boot` inputs and disk-image assumptions where applicable
- **AND** it MUST NOT rely on stale former boot source-root paths

#### Scenario: Legacy helper scripts are removed from active tooling

- **WHEN** the developer tooling migration is complete
- **THEN** top-level `tools/boot_debug.py` and `tools/install.py` MUST NOT remain as active standalone helper scripts
- **AND** active documentation MUST point to `python -m tools.bigosdev` helper commands instead
