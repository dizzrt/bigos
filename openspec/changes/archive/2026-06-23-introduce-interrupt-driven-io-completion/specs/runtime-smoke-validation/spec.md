## ADDED Requirements

### Requirement: runtime smoke 默认输出使用 logs
BigOS SHALL use `logs/` as the default repository-level directory for runtime smoke validation artifacts and per-case serial logs. The runtime smoke runner MUST keep explicit `--output` and `--serial-log-dir` values unchanged when developers provide custom paths.

#### Scenario: Matrix artifact 默认写入 logs
- **WHEN** the runtime smoke matrix runner is invoked without an explicit `--output`
- **THEN** it MUST write the Markdown-first validation artifact under `logs/`
- **AND** it MUST NOT use `log/runtime-smoke-validation.md` as the default output path

#### Scenario: Matrix per-case 串口日志默认写入 logs
- **WHEN** the runtime smoke matrix runner is invoked without an explicit `--serial-log-dir`
- **THEN** it MUST write per-case serial logs under `logs/runtime-smoke/`
- **AND** each validation result MUST record the resulting `logs/` serial log path

#### Scenario: 自定义 runtime smoke 输出路径不被改写
- **WHEN** a developer passes an explicit `--output` or `--serial-log-dir`
- **THEN** the runtime smoke runner MUST use the provided path
- **AND** it MUST NOT silently rewrite custom paths that still include `log/`

#### Scenario: runtime smoke 文档同步 logs 默认值
- **WHEN** runtime smoke validation documentation describes default artifact paths, serial log directories, or example commands
- **THEN** it MUST describe the new `logs/` defaults
- **AND** matching `docs/en` and `docs/zh` runtime smoke pages MUST stay synchronized
