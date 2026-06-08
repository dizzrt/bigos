## MODIFIED Requirements

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
- **AND** 它 MUST NOT 隐式切换为 UEFI loader、ESP 镜像或 OVMF 配置

#### Scenario: 文档描述新的 Legacy 调试入口

- **WHEN** 文档描述 Legacy BIOS 本地启动调试
- **THEN** 文档 MUST 将 `xmake run bochs` 描述为当前 Bochs SDL2 默认调试入口
- **AND** 文档 MUST 将 `xmake run bochs -- --display none` 描述为 Bochs no-GUI 调试入口
- **AND** 文档 MUST 将 `xmake run qemu -- --display none` 描述为 QEMU headless 调试入口
- **AND** 文档 MUST 将 `xmake f` 描述为 smoke 开关配置入口

### Requirement: xmake run target 负责启动

BigOS 的 `bochs`、`qemu` 和 `qemu-gdb` xmake run target SHALL 表示构建并启动对应 emulator backend，并 SHALL 将 `--` 后的 target arguments 转发给 Python helper。xmake run target 不提供 `--no-launch` run argument；离线 image 生成和校验 SHALL 继续通过 Python helper 路径提供。

#### Scenario: run target 启动 emulator

- **WHEN** 开发者运行 `xmake run bochs`、`xmake run qemu` 或 `xmake run qemu-gdb`
- **THEN** 对应 target MUST 在生成调试镜像后启动选定 emulator
- **AND** target MUST NOT 将 `--no-launch` 作为稳定 run argument

#### Scenario: run target 转发 display 参数

- **WHEN** 开发者运行 `xmake run bochs -- --display none` 或 `xmake run qemu -- --display none`
- **THEN** 对应 target MUST 将 `--display none` 转发给 Python helper
- **AND** display 校验和 emulator 命令生成 MUST 由 Python helper 负责

#### Scenario: run target 转发 helper 参数

- **WHEN** 开发者运行 `xmake run qemu -- --display none` 或 `xmake run bochs -- --display none`
- **THEN** 对应 target MUST 将 helper 参数转发给 Python helper
- **AND** target MUST preserve argument boundaries without shell re-splitting in `xmake.lua`

#### Scenario: 开发者只生成 image

- **WHEN** 开发者需要只生成和校验 raw image 而不启动 emulator
- **THEN** 文档 MUST 指向 `tools/boot_debug.py` 的 no-launch helper 路径或迁移后的等价 Python helper 命令

## ADDED Requirements

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
