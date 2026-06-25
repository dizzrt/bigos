## Purpose

Define the required one-command local boot debug workflow for BigOS, including preflight validation, kernel and boot artifact orchestration, deterministic raw disk image generation, QEMU and Bochs launch behavior, artifact isolation, preservation of existing boot runtime semantics, and xmake-owned smoke configuration.
## Requirements
### Requirement: 默认日志目录使用 logs
BigOS SHALL use `logs/` as the repository-level directory for boot debug helper serial logs, emulator diagnostic logs, and xmake run-target serial log paths. Explicitly provided log paths MUST be under `logs/`; paths outside `logs/` MUST be rejected instead of silently rewritten.

#### Scenario: Helper 默认串口日志落在 logs
- **WHEN** the boot debug helper is invoked without an explicit `--serial-log`
- **THEN** BigOS MUST choose the documented default serial log path under `logs/`
- **AND** it MUST NOT create new default serial logs under `build/test/` or `log/`

#### Scenario: xmake run target 使用 logs
- **WHEN** a developer uses the documented xmake emulator run targets that pass helper-managed serial log paths
- **THEN** those run targets MUST pass `logs/*.serial.log` paths
- **AND** they MUST NOT pass `log/*.serial.log` as the default path

#### Scenario: 显式日志路径必须位于 logs
- **WHEN** a developer explicitly passes `--serial-log`
- **THEN** the helper MUST accept the path only when it resolves under `logs/`
- **AND** it MUST reject paths under `build/test/`, `log/`, or any other non-`logs/` directory

#### Scenario: 文档示例使用 logs
- **WHEN** boot debug, memory validation, UEFI, or runtime smoke documentation shows current helper or xmake log-output examples
- **THEN** the examples MUST use `logs/` for default-style log paths
- **AND** paired `docs/en` and `docs/zh` pages MUST remain synchronized where both mirrors exist

### Requirement: One-command boot debug entry

BigOS SHALL provide documented xmake local boot debug entries that build required artifacts, prepare a bootable raw disk image, and launch the configured emulator for developer inspection. The Bochs entry SHALL be `xmake run bochs`, and Bochs display selection SHALL be controlled by target arguments forwarded after `--`.

#### Scenario: Developer starts Bochs boot debug with one command

- **WHEN** a developer runs `xmake run bochs` from the repository root
- **THEN** the command SHALL execute preflight checks, build steps, raw image preparation, generated Bochs SDL2 configuration, and Bochs launch in order

#### Scenario: Developer selects Bochs no-GUI display through xmake

- **WHEN** a developer runs `xmake run bochs -- --display none` from the repository root
- **THEN** the command SHALL execute preflight checks, build steps, raw image preparation, generated Bochs no-GUI configuration, and Bochs launch in order

#### Scenario: Command is available through a stable wrapper

- **WHEN** a developer reads the build and run documentation
- **THEN** the documentation SHALL identify `xmake run bochs` as the stable Legacy BIOS/MBR/exFAT/Bochs debug entry
- **AND** the documentation SHALL identify `xmake run bochs -- --display sdl2` and `xmake run bochs -- --display none` as the stable Bochs display selection forms
- **AND** the documentation SHALL identify `xmake f` as the stable way to configure default-off smoke options before running the debug entry

### Requirement: xmake smoke 配置保持

BigOS 的本地 boot debug 入口 SHALL 使用当前 xmake 配置构建 kernel 和调试镜像，不得在运行阶段隐式重置未显式变更的 smoke 开关。

#### Scenario: 组合 smoke 配置进入调试镜像

- **WHEN** 开发者先运行 `xmake f --user_program_smoke=y --syscall_smoke=y`，再运行 `xmake run bochs`
- **THEN** 调试入口 MUST 按当前 xmake 配置构建 kernel
- **AND** 生成的调试镜像 MUST 使用同时启用 `BIGOS_USER_PROGRAM_SMOKE` 和 `BIGOS_SYSCALL_SMOKE` 的 kernel ELF

#### Scenario: run 阶段不清除其它 smoke 配置

- **WHEN** `.xmake` 中已保存 `timer_smoke`、`user_vmem_smoke`、`syscall_smoke` 或其它默认关闭 smoke 配置
- **THEN** `xmake run bochs` MUST NOT 执行会将未列出配置恢复默认值的固定 `xmake f` 命令
- **AND** 调试入口 MUST 保留当前配置，除非开发者显式重新运行 `xmake f`

### Requirement: Preflight validation

The boot debug command SHALL validate required local tools and inputs before mutating generated images or launching the selected emulator.

#### Scenario: Required tool is missing

- **WHEN** `xmake`, `python3`, any required `x86_64-elf-*` tool, or the emulator required by the selected backend is unavailable
- **THEN** the command SHALL stop before image generation or emulator launch as appropriate
- **AND** it SHALL report the missing dependency and failed stage

#### Scenario: Build output is unavailable

- **WHEN** the kernel or boot build fails to produce a required artifact
- **THEN** the command SHALL stop before emulator launch and report which artifact is missing

### Requirement: Emulator backend selection is explicit

The Python boot debug helper SHALL make emulator selection explicit and SHALL only require the external emulator tool needed by the selected backend. The Bochs backend SHALL be selected with `bochs`; `bochs-sdl2` SHALL NOT be a supported emulator backend.

#### Scenario: QEMU backend is selected

- **WHEN** the helper is invoked for a QEMU backend
- **THEN** preflight validation SHALL require `qemu-system-x86_64`
- **AND** it SHALL NOT require `bochs` unless a Bochs backend is selected

#### Scenario: Bochs backend is selected

- **WHEN** the helper is invoked for the `bochs` backend
- **THEN** preflight validation SHALL require `bochs`
- **AND** it SHALL NOT require `qemu-system-x86_64` unless a QEMU backend is selected

#### Scenario: Removed Bochs SDL2 backend is requested

- **WHEN** the helper is invoked with `--emulator bochs-sdl2`
- **THEN** argument parsing SHALL reject the unsupported emulator backend before image generation or emulator launch

#### Scenario: No-launch image validation is requested

- **WHEN** the helper is invoked with no emulator launch requested
- **THEN** preflight validation SHALL NOT require Bochs or QEMU
- **AND** raw image generation and validation SHALL remain available without an emulator installed

### Requirement: Kernel and boot artifact build orchestration

The boot debug command SHALL build the kernel ELF and boot-stage binaries through xmake without changing boot runtime semantics.

#### Scenario: Kernel build succeeds

- **WHEN** the command runs the kernel build stage successfully
- **THEN** it SHALL use the generated kernel ELF as the root directory `kernel` file in the raw image

#### Scenario: Boot build succeeds

- **WHEN** the command runs the boot build stage successfully
- **THEN** it SHALL use generated MBR, DBR, extended DBR, and `boot.bin` artifacts for the raw image

#### Scenario: Existing build fails

- **WHEN** xmake returns a non-zero exit code while building the kernel or boot-stage artifacts
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

The boot debug command SHALL launch Bochs against the generated raw disk image for first-stage local boot debugging. The `bochs` backend SHALL support display selection through `--display`, default to SDL2 display when no display argument is supplied, and provide a no-GUI mode through `--display none`.

#### Scenario: Bochs default SDL2 display is available

- **WHEN** Bochs is installed and the developer runs `xmake run bochs`
- **THEN** the command SHALL launch Bochs with a generated configuration that points to the generated raw image
- **AND** the generated configuration SHALL include the SDL2 display selection

#### Scenario: Bochs SDL2 display is selected explicitly

- **WHEN** Bochs is installed and the developer runs `xmake run bochs -- --display sdl2`
- **THEN** the command SHALL launch Bochs with a generated configuration that points to the generated raw image
- **AND** the generated configuration SHALL include the SDL2 display selection

#### Scenario: Bochs no-GUI display is selected

- **WHEN** Bochs is installed and the developer runs `xmake run bochs -- --display none`
- **THEN** the command SHALL launch Bochs with a generated configuration that points to the generated raw image
- **AND** the generated configuration SHALL include the Bochs no-GUI display selection

#### Scenario: Bochs configuration cannot be resolved

- **WHEN** Bochs requires host-specific configuration that the command cannot infer
- **THEN** the command SHALL fail with an actionable message instead of silently launching an invalid emulator configuration

#### Scenario: Reference Bochs config is sanitized

- **WHEN** the command generates a Bochs configuration from project defaults or a reference configuration
- **THEN** it SHALL point `ata0-master` to the generated raw image and SHALL NOT hard-code host-specific Windows paths, `win32` display settings, or fixed BIOS/VGA BIOS paths

### Requirement: QEMU launch for Legacy BIOS boot debugging

BigOS SHALL provide QEMU-based xmake boot debug entries that launch the same generated Legacy BIOS/MBR/exFAT raw image used by the Bochs workflow, without changing bootloader, kernel handoff, disk image layout, or kernel runtime initialization semantics.

#### Scenario: QEMU local boot entry is available

- **WHEN** a developer runs `xmake run qemu` from the repository root
- **THEN** the command SHALL build the configured kernel and boot artifacts through xmake
- **AND** it SHALL generate or refresh the deterministic raw boot image through the Python helper
- **AND** it SHALL launch `qemu-system-x86_64` against that raw image
- **AND** it SHALL write COM1 serial output to a documented build output log path

#### Scenario: QEMU uses the existing ATA PIO compatible disk path

- **WHEN** the QEMU backend launches the generated raw image
- **THEN** it MUST expose the image through a Legacy BIOS compatible IDE disk configuration
- **AND** it MUST NOT require virtio, AHCI/SATA, NVMe, UEFI, OVMF, or a new kernel storage driver

#### Scenario: QEMU keeps Legacy BIOS runtime semantics

- **WHEN** the QEMU backend launches BigOS
- **THEN** the boot path MUST continue to use the generated MBR, DBR, extended DBR, `/boot/boot.bin`, and root `kernel`
- **AND** it MUST NOT change the kernel link address, boot handoff ABI, BootInfo location, smoke marker ABI, or kernel initialization order

### Requirement: QEMU display mode selection

BigOS SHALL provide QEMU display mode selection through the Python helper and through xmake target argument forwarding so the same QEMU backend can run with a graphical display for local inspection or without an interactive display for automated smoke tests, serial-marker checks, and CI-like validation.

#### Scenario: QEMU graphical mode is available

- **WHEN** a developer runs `xmake run qemu` without selecting headless display mode
- **THEN** the command SHALL prepare the same bootable raw image as the Bochs workflow
- **AND** it SHALL launch QEMU with graphical output suitable for observing VGA text output
- **AND** it SHALL write COM1 serial output to a documented `build/test` log path

#### Scenario: QEMU headless display mode is available through xmake

- **WHEN** the developer runs `xmake run qemu -- --display none`
- **THEN** the command SHALL prepare the same bootable raw image as the Bochs workflow
- **AND** it SHALL launch QEMU without requiring an interactive display
- **AND** it SHALL write COM1 serial output to a documented `build/test` log path

#### Scenario: QEMU headless display mode is available through the helper

- **WHEN** the Python helper is invoked for the QEMU backend with `--display none` or an equivalent headless display parameter
- **THEN** the helper SHALL prepare the same bootable raw image as the Bochs workflow
- **AND** it SHALL launch QEMU without requiring an interactive display
- **AND** it SHALL write COM1 serial output to a documented `build/test` log path

#### Scenario: Serial marker is observed under QEMU

- **WHEN** the Python helper is invoked with QEMU backend, headless display mode, `--serial-log`, and `--expect-serial-marker`
- **THEN** the helper SHALL monitor the serial log for the expected marker
- **AND** it SHALL terminate the QEMU process group after the marker is observed
- **AND** it SHALL report success without requiring manual emulator shutdown

#### Scenario: Serial marker is not observed under QEMU

- **WHEN** the expected marker is not observed before the configured smoke timeout
- **THEN** the helper SHALL terminate the QEMU process group
- **AND** it SHALL fail with a message that identifies the missing marker, serial log path, and QEMU smoke stage

### Requirement: QEMU GDB debug entry

BigOS SHALL provide a QEMU GDB-oriented debug entry that starts the generated Legacy BIOS image paused with a QEMU GDB stub.

#### Scenario: GDB debug command is available

- **WHEN** a developer runs `xmake run qemu-gdb`
- **THEN** the command SHALL prepare the same bootable raw image as the other boot debug entries
- **AND** it SHALL launch QEMU with a GDB stub enabled
- **AND** it SHALL pause CPU execution before boot continues
- **AND** it SHALL use graphical output by default so VGA text output remains visible during GDB debugging
- **AND** it SHALL report the expected GDB attachment target or document the default QEMU GDB port

#### Scenario: GDB entry is not used for automatic smoke

- **WHEN** documentation describes smoke-test commands
- **THEN** it SHALL identify QEMU backend with headless display mode as the preferred automated QEMU smoke path
- **AND** it SHALL NOT describe `xmake run qemu-gdb` as a non-interactive smoke command

### Requirement: Legacy BIOS 调试语义保持稳定

BigOS SHALL 将 Legacy BIOS/MBR/exFAT/Bochs/QEMU 调试语义迁移到 xmake 入口，同时保持 boot protocol、镜像布局、kernel 文件名、bootloader lookup 和 kernel 初始化顺序不变。

#### Scenario: xmake 默认 Bochs 入口启动 Legacy BIOS 路径

- **WHEN** 开发者运行 `xmake run bochs`
- **THEN** 该命令 MUST 构建并启动由 MBR、DBR、extended DBR、`boot.bin` 和 root `kernel` 组成的 Legacy BIOS 调试路径
- **AND** 它 MUST 默认注入 SDL2 display 配置
- **AND** 它 MUST NOT 隐式切换为 UEFI loader、ESP 镜像或 OVMF 配置

#### Scenario: xmake Bochs no-GUI 入口启动 Legacy BIOS 路径

- **WHEN** 开发者运行 `xmake run bochs -- --display none`
- **THEN** 该命令 MUST 构建并启动由 MBR、DBR、extended DBR、`boot.bin` 和 root `kernel` 组成的 Legacy BIOS 调试路径
- **AND** 它 MUST 注入 Bochs no-GUI display 配置

#### Scenario: 文档描述新的 Legacy 调试入口

- **WHEN** 文档描述 Legacy BIOS 本地启动调试
- **THEN** 文档 MUST 将 `xmake run bochs` 描述为当前 Bochs SDL2 默认调试入口
- **AND** 文档 MUST 将 `xmake run bochs -- --display none` 描述为 Bochs no-GUI 调试入口
- **AND** 文档 MUST 将 `xmake run qemu -- --display none` 描述为 QEMU headless 调试入口
- **AND** 文档 MUST 将 `xmake f` 描述为 smoke 开关配置入口

### Requirement: Python helper 不提供 smoke 快捷配置

BigOS 的 Python boot debug helper SHALL 不再提供 `--memory-self-test`、`--user-program-smoke` 或等价 smoke 快捷配置参数；开发者 MUST 先使用 `xmake f` 配置 smoke 开关。

#### Scenario: 开发者配置 memory self-test

- **WHEN** 开发者需要运行 memory self-test 调试镜像
- **THEN** 文档和工具提示 MUST 引导开发者先运行 `xmake f --mm_self_test=y`
- **AND** Python helper MUST NOT 通过 `--memory-self-test` 参数自行修改 xmake 配置

#### Scenario: 开发者配置 user program smoke

- **WHEN** 开发者需要运行 first user program smoke 调试镜像
- **THEN** 文档和工具提示 MUST 引导开发者先运行 `xmake f --user_program_smoke=y`
- **AND** Python helper MUST NOT 通过 `--user-program-smoke` 参数自行修改 xmake 配置

### Requirement: xmake run target 负责启动

BigOS 的 `bochs`、`qemu`、`qemu-legacy`、`qemu-gdb` 和 `qemu-uefi` xmake run target SHALL 表示构建并启动对应 emulator backend，并 SHALL 将 `--` 后的 target arguments 转发给 `tools.bigosdev` Python helper。xmake run target 不提供 `--no-launch` run argument；离线 image 生成和校验 SHALL 继续通过 `tools.bigosdev` helper 路径提供。

#### Scenario: run target 启动 emulator

- **WHEN** 开发者运行 `xmake run bochs`、`xmake run qemu`、`xmake run qemu-legacy`、`xmake run qemu-gdb` 或 `xmake run qemu-uefi`
- **THEN** 对应 target MUST 在生成调试镜像后启动选定 emulator
- **AND** target MUST NOT 将 `--no-launch` 作为稳定 run argument

#### Scenario: run target 转发 display 参数

- **WHEN** 开发者运行 `xmake run bochs -- --display none` 或 `xmake run qemu -- --display none`
- **THEN** 对应 target MUST 将 `--display none` 转发给 `tools.bigosdev` Python helper
- **AND** display 校验和 emulator 命令生成 MUST 由 Python helper 负责

#### Scenario: run target 转发 helper 参数

- **WHEN** 开发者运行 `xmake run qemu -- --display none` 或 `xmake run bochs -- --display none`
- **THEN** 对应 target MUST 将 helper 参数转发给 `tools.bigosdev` Python helper
- **AND** target MUST preserve argument boundaries without shell re-splitting in `xmake.lua`

#### Scenario: 开发者只生成 image

- **WHEN** 开发者需要只生成和校验 raw image 而不启动 emulator
- **THEN** 文档 MUST 指向 `uv run python -m tools.bigosdev image create`、`uv run python -m tools.bigosdev image validate` 或等价 Python helper 命令

### Requirement: Unified emulator display argument validation

The Python boot debug helper SHALL validate display arguments according to the selected emulator backend and SHALL reject unsupported emulator/display combinations before emulator launch.

#### Scenario: Bochs accepts SDL2 display

- **WHEN** the helper is invoked with `--emulator bochs --display sdl2`
- **THEN** it SHALL generate a Bochs configuration that selects SDL2 display

#### Scenario: Bochs accepts no-GUI display

- **WHEN** the helper is invoked with `--emulator bochs --display none`
- **THEN** it SHALL generate a Bochs configuration that selects no-GUI display

#### Scenario: QEMU accepts graphical display

- **WHEN** the helper is invoked with `--emulator qemu --display graphical`
- **THEN** it SHALL launch QEMU with graphical output suitable for observing VGA text output

#### Scenario: QEMU accepts no-display mode

- **WHEN** the helper is invoked with `--emulator qemu --display none`
- **THEN** it SHALL launch QEMU with `-display none` or an equivalent no-display configuration

#### Scenario: Unsupported display combination is rejected

- **WHEN** the helper is invoked with `--emulator qemu --display sdl2` or `--emulator bochs --display graphical`
- **THEN** it SHALL reject the unsupported combination before emulator launch

### Requirement: Generated artifacts remain isolated

The boot debug workflow SHALL place generated images, emulator configs, serial logs, and temporary files under build or test output paths so source files and hand-written OpenSpec artifacts are not overwritten.

#### Scenario: Command regenerates boot debug artifacts

- **WHEN** the boot debug command is run repeatedly
- **THEN** it SHALL overwrite only documented generated outputs or an explicitly specified image path

#### Scenario: Developer wants to inspect artifacts

- **WHEN** the command completes image generation
- **THEN** it SHALL report the generated raw image path and the selected emulator's generated configuration or launch-relevant output paths

#### Scenario: QEMU serial log is generated

- **WHEN** a QEMU backend is launched with serial logging enabled
- **THEN** the serial log SHALL be written under a documented `logs/` path
- **AND** an explicitly specified serial log path outside `logs/` SHALL be rejected

### Requirement: Runtime behavior preservation

The one-command boot debug workflow SHALL NOT change BigOS boot protocol behavior, kernel link addresses, boot handoff data, or kernel initialization order.

#### Scenario: Boot protocol remains unchanged

- **WHEN** the new workflow prepares and launches the image
- **THEN** the bootloader SHALL still load `/boot/boot.bin`, find root `kernel`, load ELF64 segments, and jump to the existing kernel entry using the existing address assumptions

#### Scenario: Kernel runtime remains unchanged

- **WHEN** the workflow reaches the kernel
- **THEN** kernel initialization SHALL follow the existing `kernel()` path without requiring new runtime dependencies

### Requirement: UEFI 调试入口独立规划

未来 UEFI 本地启动调试入口 SHALL 使用独立命名和独立镜像/模拟器配置规划。

#### Scenario: 规划 UEFI 调试命令

- **WHEN** 项目级路线图描述未来 UEFI 启动调试
- **THEN** 它 MUST 使用独立入口名称，例如 `xmake run qemu-ovmf` 或等价 project-level xmake wrapper
- **AND** 它 MUST 明确该入口将使用 UEFI loader、ESP/FAT 镜像和 QEMU + OVMF 作为首选 UEFI 固件配置

#### Scenario: Legacy 与 UEFI 模拟器选择明确

- **WHEN** 文档描述本地启动调试矩阵
- **THEN** 它 MUST 明确 Legacy BIOS 路径继续使用 Bochs，例如 `xmake run bochs`、`xmake run bochs -- --display sdl2` 或 `xmake run bochs -- --display none`
- **AND** 它 MUST 明确 UEFI smoke test 首选 QEMU + OVMF，Bochs UEFI 仅作为可选验证路径

#### Scenario: 两类调试产物隔离

- **WHEN** 后续 change 实现 UEFI 启动调试入口
- **THEN** 它 MUST 将 UEFI 镜像、固件配置和临时产物与现有 BIOS raw image/Bochs 配置隔离
- **AND** 它 MUST NOT 覆盖 `xmake run bochs` 生成的 Legacy BIOS 调试产物，除非用户显式指定同一路径

### Requirement: Emulator documentation remains scenario-specific

BigOS documentation SHALL describe QEMU and Bochs as complementary local debug backends with distinct recommended use cases.

#### Scenario: Documentation describes QEMU use cases

- **WHEN** documentation describes local boot validation and smoke testing
- **THEN** it SHALL identify QEMU backend with headless display mode as the preferred automated smoke and serial-marker path
- **AND** it SHALL identify `xmake run qemu` with graphical display as the preferred quick local QEMU boot validation command
- **AND** it SHALL identify `xmake run qemu-gdb` as the QEMU GDB stub debug command

#### Scenario: Documentation preserves Bochs use cases

- **WHEN** documentation describes early boot, BIOS, real-mode, protected-mode, long-mode transition, ATA PIO, interrupt, or hardware behavior investigations
- **THEN** it SHALL continue to identify `xmake run bochs`, `xmake run bochs -- --display sdl2`, and `xmake run bochs -- --display none` as supported Bochs debug entries
- **AND** it SHALL recommend Bochs or dual-emulator cross-checking for high-risk low-level changes when the environment supports it

#### Scenario: Documentation separates QEMU Legacy BIOS from future UEFI

- **WHEN** documentation describes the QEMU backend introduced by this change
- **THEN** it MUST describe the backend as a Legacy BIOS/MBR/exFAT QEMU path
- **AND** it MUST NOT imply that UEFI loader, ESP/FAT image generation, or OVMF boot is implemented by this change

### Requirement: UEFI QEMU/OVMF debug entry

BigOS SHALL provide an explicit UEFI QEMU/OVMF local debug entry that builds UEFI artifacts, prepares an ESP/FAT image, and launches QEMU with x86_64 OVMF firmware.

#### Scenario: Developer starts UEFI debug entry

- **WHEN** a developer invokes the documented UEFI debug entry from the repository root
- **THEN** the command MUST build the configured kernel, user payloads, and `BOOTX64.EFI`
- **AND** it MUST generate or refresh the UEFI ESP image
- **AND** it MUST launch `qemu-system-x86_64` with x86_64 OVMF firmware and the generated ESP image

#### Scenario: UEFI debug entry uses headless serial mode

- **WHEN** the UEFI debug entry is run in headless mode
- **THEN** it MUST configure QEMU without requiring an interactive display
- **AND** it MUST write COM1 serial output to a documented UEFI-specific log path under the build/test output area unless explicitly overridden

#### Scenario: UEFI debug entry validates default runtime path

- **WHEN** the UEFI debug entry runs the default boot configuration with marker validation enabled
- **THEN** it MUST wait for the same default init/user exec marker used by the Legacy BIOS default headless path
- **AND** missing that marker, including the current `BIGOS_USER_EXEC` baseline marker, MUST be reported as a failed or blocked UEFI runtime-parity check rather than success

#### Scenario: OVMF vars are writable copy

- **WHEN** the UEFI debug entry prepares QEMU firmware inputs
- **THEN** it MUST use the OVMF code firmware as read-only input
- **AND** it MUST copy the OVMF vars template to a generated writable output path before launch
- **AND** it MUST NOT mutate the package-manager-installed vars template in place

### Requirement: UEFI debug preflight

The UEFI debug entry SHALL validate UEFI-specific local dependencies before mutating generated images or launching QEMU.

#### Scenario: OVMF code firmware is missing

- **WHEN** the configured or auto-detected x86_64 OVMF code firmware is unavailable
- **THEN** the UEFI debug entry MUST stop before QEMU launch
- **AND** it MUST report the missing firmware path and the UEFI preflight stage

#### Scenario: OVMF vars template is missing

- **WHEN** the configured or auto-detected OVMF vars template is unavailable
- **THEN** the UEFI debug entry MUST stop before QEMU launch
- **AND** it MUST report the missing vars template and the UEFI preflight stage

#### Scenario: ESP tooling is missing

- **WHEN** required FAT image tools are unavailable
- **THEN** the UEFI debug entry MUST stop before ESP image generation
- **AND** it MUST report the missing tool and the ESP generation stage

### Requirement: Legacy and UEFI debug outputs remain isolated

BigOS SHALL keep UEFI debug outputs separate from existing Legacy BIOS debug outputs.

#### Scenario: Existing Legacy QEMU entry remains BIOS

- **WHEN** a developer invokes the existing QEMU Legacy BIOS entry
- **THEN** it MUST continue to generate and launch the Legacy BIOS/MBR/exFAT raw image
- **AND** it MUST NOT use OVMF, ESP/FAT images, `BOOTX64.EFI`, or UEFI-specific vars files

#### Scenario: Existing Bochs entry remains BIOS

- **WHEN** a developer invokes the existing Bochs entry
- **THEN** it MUST continue to generate and launch the Legacy BIOS/MBR/exFAT raw image
- **AND** it MUST NOT depend on OVMF, ESP/FAT images, or the UEFI loader artifact

#### Scenario: Artifact paths are distinct

- **WHEN** both Legacy BIOS and UEFI debug entries are used in the same workspace
- **THEN** generated raw images, ESP images, firmware vars copies, emulator configs, and serial logs MUST use distinct documented paths
- **AND** rerunning one backend MUST NOT silently delete or overwrite the other backend's generated outputs except through documented cleanup behavior

### Requirement: Python helper path uses bigosdev package
BigOS SHALL use `uv run python -m tools.bigosdev` as the stable documented Python helper entry for local image generation, emulator launch, marker waiting, and image validation.

#### Scenario: Documentation shows direct helper commands
- **WHEN** active README, docs, AGENTS guidance, or OpenSpec specs show direct Python helper commands for boot image generation or emulator launch
- **THEN** the commands MUST use `uv run python -m tools.bigosdev`
- **AND** they MUST NOT use `uv run python tools/boot_debug.py` as an active supported command

#### Scenario: Helper supports no-launch workflow through image subcommands
- **WHEN** a developer needs to generate or validate a boot image without launching an emulator
- **THEN** the helper MUST provide documented image-only subcommands under `tools.bigosdev image`
- **AND** those subcommands MUST preserve the existing Legacy and UEFI image validation behavior

