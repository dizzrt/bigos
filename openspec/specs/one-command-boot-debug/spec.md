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

### Requirement: Legacy boot-debug 命令语义保持稳定

BigOS SHALL 保持 `make boot-debug` 作为 Legacy BIOS/MBR/exFAT 本地启动调试入口，UEFI 蓝图不得改变该命令的固件协议语义。

#### Scenario: boot-debug 继续启动 BIOS 路径

- **WHEN** 开发者运行 `make boot-debug`
- **THEN** 该命令 MUST 继续构建并启动现有 MBR、DBR、extended DBR、`boot.bin` 和 root `kernel` 组成的 Legacy BIOS 调试路径
- **AND** 它 MUST NOT 隐式切换为 UEFI loader、ESP 镜像或 OVMF 配置

#### Scenario: 文档描述 boot-debug 范围

- **WHEN** 文档描述 `make boot-debug`
- **THEN** 文档 MUST 明确该命令服务 Legacy BIOS 调试路径
- **AND** 文档 MUST 将 UEFI 调试入口标记为未来独立命令规划，而不是当前命令的替代语义

### Requirement: UEFI 调试入口独立规划

未来 UEFI 本地启动调试入口 SHALL 使用独立命名和独立镜像/模拟器配置规划。

#### Scenario: 规划 UEFI 调试命令

- **WHEN** 项目级路线图描述未来 UEFI 启动调试
- **THEN** 它 MUST 使用独立入口名称，例如 `make uefi-boot-debug` 或等价 project-level wrapper
- **AND** 它 MUST 明确该入口将使用 UEFI loader、ESP/FAT 镜像和 QEMU + OVMF 作为首选 UEFI 固件配置

#### Scenario: Legacy 与 UEFI 模拟器选择明确

- **WHEN** 文档描述本地启动调试矩阵
- **THEN** 它 MUST 明确 Legacy BIOS 路径继续使用 Bochs
- **AND** 它 MUST 明确 UEFI smoke test 首选 QEMU + OVMF，Bochs UEFI 仅作为可选验证路径

#### Scenario: 两类调试产物隔离

- **WHEN** 后续 change 实现 UEFI 启动调试入口
- **THEN** 它 MUST 将 UEFI 镜像、固件配置和临时产物与现有 BIOS raw image/Bochs 配置隔离
- **AND** 它 MUST NOT 覆盖 `make boot-debug` 生成的 Legacy BIOS 调试产物，除非用户显式指定同一路径
