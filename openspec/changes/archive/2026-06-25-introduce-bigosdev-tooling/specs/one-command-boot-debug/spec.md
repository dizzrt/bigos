## MODIFIED Requirements

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

## ADDED Requirements

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
