## MODIFIED Requirements

### Requirement: Runtime smoke runner uses QEMU headless marker checks

BigOS SHALL provide a validation runner through `tools.bigosdev smoke matrix` or an equivalent documented helper flow that executes matrix cases through the supported generated image paths and prefers QEMU headless serial-marker checks for automated validation.

#### Scenario: Runner executes a matrix case

- **WHEN** the runner executes a runtime smoke matrix case
- **THEN** it MUST configure the case-specific smoke switches through `xmake f`
- **AND** it MUST build the configured kernel and boot artifacts through the xmake-backed flow
- **AND** it MUST launch the QEMU backend with headless display mode or an equivalent `tools.bigosdev run --emulator qemu --display none` helper path
- **AND** it MUST wait for the case's expected serial marker within the case-specific bounded timeout

#### Scenario: Expected marker is observed

- **WHEN** the runner observes the expected serial marker for a case
- **THEN** it MUST mark that case as passed
- **AND** it MUST record the serial log path and observed marker in the validation artifact

#### Scenario: Expected marker is missing

- **WHEN** the runner does not observe the expected serial marker before timeout or emulator exit
- **THEN** it MUST mark that case as failed
- **AND** it MUST record the missing marker, serial log path, timeout or exit status, and failed stage

## ADDED Requirements

### Requirement: Runtime smoke documentation uses bigosdev entry
BigOS runtime smoke validation documentation SHALL use `uv run python -m tools.bigosdev smoke matrix` and `uv run python -m tools.bigosdev run` as the active Python helper examples.

#### Scenario: Documentation shows runtime smoke commands
- **WHEN** runtime smoke validation documentation, AGENTS guidance, README examples, or OpenSpec active specs show current runtime smoke commands
- **THEN** they MUST use `tools.bigosdev` helper paths
- **AND** they MUST NOT show `tools/boot_debug.py runtime-smoke-matrix` as an active supported command
