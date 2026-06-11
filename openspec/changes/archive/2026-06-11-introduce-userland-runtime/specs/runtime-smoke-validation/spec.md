## ADDED Requirements

### Requirement: 用户态运行时验证开关 userland_smoke

BigOS SHALL 新增一个默认关闭的构建开关 `userland_smoke`（定义 `BIGOS_USERLAND_SMOKE`），用于在受控构建中验证用户态运行时端到端路径，并发射固定 COM1/VGA marker `BIGOS_USERLAND_PASSED` 或 `BIGOS_USERLAND_FAILED`。该开关 MUST 默认关闭，MUST NOT 改动或删除既有 smoke 开关与其 marker，且 MUST NOT 成为 normal boot 的一部分。

#### Scenario: 开关默认关闭

- **WHEN** BigOS 以默认配置构建
- **THEN** `BIGOS_USERLAND_SMOKE` MUST NOT 被定义
- **AND** 既有 smoke 开关与 marker 行为 MUST 保持不变

#### Scenario: 开启后发射通过 marker

- **WHEN** 以 `userland_smoke=y` 构建并在 QEMU headless 下启动，且用户态运行时路径（crt0 传参与退出码、libc syscall wrapper 与 errno 翻译、shell `fork`+`execve`+`wait`、单级管道与重定向、最小 `malloc`/`free`）全部通过
- **THEN** 内核或用户态验证程序 MUST 发射 `BIGOS_USERLAND_PASSED`

#### Scenario: 失败时发射失败 marker

- **WHEN** 用户态运行时验证路径中任一断言失败
- **THEN** MUST 发射 `BIGOS_USERLAND_FAILED`（可附带失败原因）
- **AND** MUST NOT 静默通过或发射 `BIGOS_USERLAND_PASSED`
