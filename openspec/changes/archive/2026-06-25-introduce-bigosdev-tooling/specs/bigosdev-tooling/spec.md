## ADDED Requirements

### Requirement: bigosdev package is the stable Python developer tooling entry
BigOS SHALL provide `tools/bigosdev/` as the stable executable Python package for repository developer tooling that prepares boot images, launches emulators, patches supported boot artifacts, and runs runtime smoke validation.

#### Scenario: Developer invokes the package entry
- **WHEN** a developer runs `uv run python -m tools.bigosdev --help` from the repository root
- **THEN** the command MUST dispatch through `tools/bigosdev/__main__.py` and `tools/bigosdev/cli.py`
- **AND** it MUST list the supported developer tooling subcommands

#### Scenario: Old single-file helpers are removed
- **WHEN** the bigosdev tooling migration is complete
- **THEN** `tools/boot_debug.py` MUST NOT remain as an active helper entry
- **AND** `tools/install.py` MUST NOT remain as an active helper entry
- **AND** project-owned active commands MUST use `uv run python -m tools.bigosdev ...` or `python3 -m tools.bigosdev ...`

### Requirement: bigosdev modules are separated by responsibility
The bigosdev implementation SHALL separate command parsing, shared configuration, process/tool handling, build artifacts, image handling, emulator handling, and runtime smoke validation into dedicated modules.

#### Scenario: Developer inspects package structure
- **WHEN** a developer lists `tools/bigosdev/`
- **THEN** CLI entry logic MUST be discoverable in `cli.py` and `__main__.py`
- **AND** shared paths, constants, errors, process helpers, build artifacts, font asset conversion, image logic, emulator logic, and smoke logic MUST be discoverable through dedicated modules or subpackages

#### Scenario: Image logic is layered
- **WHEN** a developer inspects `tools/bigosdev/image/`
- **THEN** shared exFAT parsing/generation logic, Legacy image creation/validation, UEFI image creation/validation, image patching, and persistent image creation MUST be separated by module boundaries

#### Scenario: Emulator logic is isolated
- **WHEN** a developer inspects `tools/bigosdev/emulator/`
- **THEN** QEMU-specific command construction and Bochs-specific configuration/launch behavior MUST be separated
- **AND** xmake run target files MUST NOT duplicate QEMU or Bochs command generation details

### Requirement: image patch preserves bounded install semantics
BigOS SHALL provide `tools.bigosdev image patch` as the supported way to patch boot artifacts into an existing supported BIOS/MBR/exFAT image.

#### Scenario: Developer patches boot artifacts
- **WHEN** a developer runs `uv run python -m tools.bigosdev image patch --image <raw> --with-mbr <mbr> --with-dbr <dbr> --with-exdbr <exdbr> --with-boot <boot>`
- **THEN** the command MUST patch only the requested boot artifacts into the existing image
- **AND** it MUST preserve the existing bounded write semantics previously required for the boot artifact installer

#### Scenario: Unsupported boot file placement is rejected
- **WHEN** `/boot/boot.bin` is missing, non-contiguous, too small, or located through an unsupported exFAT directory layout
- **THEN** `tools.bigosdev image patch --with-boot` MUST fail with an explicit unsupported-layout or equivalent stage-aware diagnostic
- **AND** it MUST NOT create files, extend files, allocate clusters, update allocation bitmap entries, or partially update the image

### Requirement: bigosdev preserves boot and runtime contracts
The bigosdev migration SHALL NOT change boot protocol behavior, disk image layout, emulator device exposure, kernel handoff ABI, runtime smoke marker strings, or default-off smoke switches.

#### Scenario: Developer runs an existing xmake emulator target after migration
- **WHEN** a developer runs an existing supported xmake emulator target such as `xmake run qemu`, `xmake run qemu-legacy`, `xmake run qemu-gdb`, `xmake run qemu-uefi`, or `xmake run bochs`
- **THEN** the target MUST call `python3 -m tools.bigosdev run` with the appropriate backend arguments
- **AND** the selected backend MUST preserve the pre-migration boot mode, image path, serial log path, and argument forwarding behavior

#### Scenario: Runtime markers remain source of truth
- **WHEN** bigosdev waits for an expected serial marker
- **THEN** it MUST treat the existing COM1/VGA marker behavior as the validation source of truth
- **AND** it MUST NOT reinterpret a missing expected marker as success
