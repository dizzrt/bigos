## Purpose

Define the required one-command local boot debug workflow for BigOS, including preflight validation, kernel and boot artifact orchestration, deterministic raw disk image generation, Bochs launch behavior, artifact isolation, and preservation of existing boot runtime semantics.

## Requirements

### Requirement: One-command boot debug entry

BigOS SHALL provide a documented one-command local boot debug entry that builds required artifacts, prepares a bootable raw disk image, and launches the configured emulator for developer inspection.

#### Scenario: Developer starts boot debug with one command

- **WHEN** a developer runs the documented boot debug command from the repository root
- **THEN** the command SHALL execute preflight checks, build steps, raw image preparation, and Bochs launch in order

#### Scenario: Command is available through a stable wrapper

- **WHEN** a developer reads the build and run documentation
- **THEN** the documentation SHALL identify a stable one-command entry such as `make boot-debug` or an equivalent project-level wrapper

### Requirement: Preflight validation

The boot debug command SHALL validate required local tools and inputs before mutating generated images or launching the emulator.

#### Scenario: Required tool is missing

- **WHEN** `xmake`, `python3`, `bochs`, or any required `x86_64-elf-*` tool is unavailable
- **THEN** the command SHALL stop before image generation and report the missing dependency and failed stage

#### Scenario: Build output is unavailable

- **WHEN** the kernel or boot build fails to produce a required artifact
- **THEN** the command SHALL stop before emulator launch and report which artifact is missing

### Requirement: Kernel and boot artifact build orchestration

The boot debug command SHALL build the kernel ELF and boot-stage binaries using the existing project build systems without changing boot runtime semantics.

#### Scenario: Kernel build succeeds

- **WHEN** the command runs the kernel build stage successfully
- **THEN** it SHALL use the generated kernel ELF as the root directory `kernel` file in the raw image

#### Scenario: Boot build succeeds

- **WHEN** the command runs the boot build stage successfully
- **THEN** it SHALL use generated MBR, DBR, extended DBR, and `boot.bin` artifacts for the raw image

#### Scenario: Existing build fails

- **WHEN** `xmake` or the boot Makefile returns a non-zero exit code
- **THEN** the command SHALL preserve and surface the build failure instead of continuing with stale artifacts

### Requirement: User-space raw disk image generation

The boot debug command SHALL generate or refresh a fixed raw disk image entirely in user space, without requiring host disk mounting, `diskutil`, loop devices, or filesystem formatting commands.

#### Scenario: Raw image is generated

- **WHEN** build artifacts are available
- **THEN** the command SHALL create a raw disk image under the build output area with a deterministic size and layout

#### Scenario: Host mount tools are unavailable

- **WHEN** macOS `diskutil`, Linux loop devices, or exFAT formatting tools are unavailable
- **THEN** the command SHALL still be able to generate the raw image using only repository scripts and Python standard library capabilities

### Requirement: Boot-compatible exFAT layout

The generated raw image SHALL contain an exFAT partition layout compatible with the existing BigOS bootloader lookup and loading assumptions.

#### Scenario: Required boot files are present

- **WHEN** the raw image generation finishes successfully
- **THEN** the image SHALL contain an active exFAT partition with `/boot/boot.bin` and a root directory `kernel` file

#### Scenario: Files are stored contiguously

- **WHEN** the bootloader scans the generated exFAT directories
- **THEN** `/boot/boot.bin` and `kernel` SHALL be represented as contiguous files that the existing bootloader can load

#### Scenario: Boot regions are installed

- **WHEN** the raw image generation finishes successfully
- **THEN** the image SHALL contain the generated MBR, exFAT DBR, extended DBR, and backup exFAT boot region data required by the current boot flow

### Requirement: Bochs launch for first-stage debugging

The boot debug command SHALL launch Bochs against the generated raw disk image for first-stage local boot debugging.

#### Scenario: Bochs is available

- **WHEN** Bochs is installed and the raw image is prepared
- **THEN** the command SHALL launch Bochs with a generated or documented configuration that points to the generated raw image

#### Scenario: Bochs configuration cannot be resolved

- **WHEN** Bochs requires host-specific configuration that the command cannot infer
- **THEN** the command SHALL fail with an actionable message instead of silently launching an invalid emulator configuration

#### Scenario: Reference Bochs config is sanitized

- **WHEN** the command generates a Bochs configuration from project defaults or a reference configuration
- **THEN** it SHALL point `ata0-master` to the generated raw image and SHALL NOT hard-code host-specific Windows paths, `win32` display settings, or fixed BIOS/VGA BIOS paths

### Requirement: Generated artifacts remain isolated

The boot debug workflow SHALL place generated images, emulator configs, and temporary files under build or test output paths so source files and hand-written OpenSpec artifacts are not overwritten.

#### Scenario: Command regenerates boot debug artifacts

- **WHEN** the boot debug command is run repeatedly
- **THEN** it SHALL overwrite only documented generated outputs or an explicitly specified image path

#### Scenario: Developer wants to inspect artifacts

- **WHEN** the command completes image generation
- **THEN** it SHALL report the generated raw image path and Bochs configuration path

### Requirement: Runtime behavior preservation

The one-command boot debug workflow SHALL NOT change BigOS boot protocol behavior, kernel link addresses, boot handoff data, or kernel initialization order.

#### Scenario: Boot protocol remains unchanged

- **WHEN** the new workflow prepares and launches the image
- **THEN** the bootloader SHALL still load `/boot/boot.bin`, find root `kernel`, load ELF64 segments, and jump to the existing kernel entry using the existing address assumptions

#### Scenario: Kernel runtime remains unchanged

- **WHEN** the workflow reaches the kernel
- **THEN** kernel initialization SHALL follow the existing `kernel()` path without requiring new runtime dependencies
