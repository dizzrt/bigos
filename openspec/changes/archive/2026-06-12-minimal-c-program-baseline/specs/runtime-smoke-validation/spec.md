## ADDED Requirements

### Requirement: 简单 C 程序行为断言覆盖 简单 C 程序基线

BigOS runtime validation SHALL provide behavior-oriented checks for the simple C program baseline. These checks MUST validate runtime-observable behavior such as argument handoff, environment handoff, stdout/stderr output, `errno` translation, process exit status, and shell execution of packaged C programs.

#### Scenario: 参数和环境行为可验证

- **WHEN** simple C program baseline runtime validation runs the simple C program baseline
- **THEN** validation MUST observe that a packaged C program receives expected `argc`/`argv`
- **AND** validation MUST observe that environment handoff is present or deterministically reported as absent within the documented boundary

#### Scenario: wrapper 和错误报告行为可验证

- **WHEN** simple C program baseline runtime validation exercises a failing libc wrapper path
- **THEN** validation MUST observe the documented failure return and `errno` behavior through program output, exit status, or another runtime-visible result

#### Scenario: shell 执行小型 C 程序可验证

- **WHEN** simple C program baseline runtime validation invokes packaged C programs through `/bin/sh` or an equivalent deterministic shell path
- **THEN** validation MUST observe program stdout/stderr and exit behavior
- **AND** validation MUST confirm the shell continues after the external program exits

### Requirement: 简单 C 程序基线验证保持分层和默认关闭

BigOS SHALL keep simple C program baseline validation layered with existing source, build, runtime, and environment-dependent checks. Emulator-dependent validation MUST remain optional or default-off unless the surrounding test mode explicitly enables it.

#### Scenario: 默认构建不强制运行 emulator smoke

- **WHEN** a normal default build is requested
- **THEN** simple C program baseline emulator smoke MUST NOT be mandatory for the build to complete

#### Scenario: 环境缺失时记录残留风险

- **WHEN** QEMU、Bochs、交叉工具链、显示或串口日志环境不可用
- **THEN** validation notes MUST record the skipped check, substitute checks, and residual risk instead of claiming runtime validation passed
