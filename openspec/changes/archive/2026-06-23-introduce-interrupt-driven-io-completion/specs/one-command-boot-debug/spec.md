## ADDED Requirements

### Requirement: 默认日志目录使用 logs
BigOS SHALL use `logs/` as the default repository-level directory for boot debug helper serial logs, emulator diagnostic logs, and xmake run-target serial log paths. Explicitly provided log paths MUST continue to be honored without automatic rewriting.

#### Scenario: Helper 默认串口日志落在 logs
- **WHEN** the boot debug helper is invoked without an explicit `--serial-log`
- **THEN** BigOS MUST choose the documented default serial log path under `logs/`
- **AND** it MUST NOT create new default serial logs under `log/`

#### Scenario: xmake run target 使用 logs
- **WHEN** a developer uses the documented xmake emulator run targets that pass helper-managed serial log paths
- **THEN** those run targets MUST pass `logs/*.serial.log` paths
- **AND** they MUST NOT pass `log/*.serial.log` as the default path

#### Scenario: 显式日志路径保持原样
- **WHEN** a developer explicitly passes `--serial-log`, `--output`, or another documented log output path
- **THEN** the helper MUST use that path as provided
- **AND** it MUST NOT rewrite a user-specified custom path from `log/` to `logs/`

#### Scenario: 文档示例使用 logs
- **WHEN** boot debug, memory validation, UEFI, or runtime smoke documentation shows current helper or xmake log-output examples
- **THEN** the examples MUST use `logs/` for default-style log paths
- **AND** paired `docs/en` and `docs/zh` pages MUST remain synchronized where both mirrors exist
