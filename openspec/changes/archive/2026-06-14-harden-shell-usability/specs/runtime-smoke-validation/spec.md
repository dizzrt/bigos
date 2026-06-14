## ADDED Requirements

### Requirement: Runtime validation covers shell usability hardening

BigOS SHALL provide behavior-oriented validation for hardened shell usability by extending the existing `userland_smoke` path rather than adding a separate shell usability smoke switch. Validation MUST cover at least one successful command path, one deterministic command or syntax failure, one cwd-relative path command, one output redirection path, one single-pipe composition path, one bounded exit-status observation path, and recovery by running a subsequent command after failure. Validation MUST preserve existing runtime smoke defaults and MUST NOT require unrelated smoke switches unless explicitly documented by the selected case.

#### Scenario: 验证覆盖成功和失败 shell 行为

- **WHEN** shell usability validation runs in an environment with required cross-toolchain, image packaging, shell/userland, and emulator support
- **THEN** it MUST observe a supported command success, a deterministic unsupported or missing-command failure, and a subsequent command that proves shell recovery
- **AND** each result MUST be decidable through stdout/stderr, bounded exit status, serial/log output, or another deterministic low-level signal

#### Scenario: 验证覆盖 redirection 与 pipe 组合

- **WHEN** shell usability validation exercises supported redirection and single-pipe behavior
- **THEN** it MUST observe that redirected output reaches the intended writable runtime path or pipe data reaches the downstream command
- **AND** unrelated parent shell fd state MUST remain usable for a later observable command

#### Scenario: 验证覆盖 bounded status

- **WHEN** validation runs commands that succeed, fail during shell setup, fail during exec, or exit nonzero
- **THEN** the validation path MUST distinguish success from failure through a shell-local bounded status or deterministic output
- **AND** it MUST NOT require full POSIX `$?`, shell variables, scripts, job control, or terminal process groups

#### Scenario: 验证扩展现有 userland smoke

- **WHEN** shell usability runtime validation is added
- **THEN** it MUST extend the existing `userland_smoke` coverage instead of requiring a new shell usability smoke switch
- **AND** the validation matrix or notes MUST describe which shell usability cases are covered by that existing smoke path

### Requirement: Shell usability validation records environment-dependent skips

BigOS SHALL record executed, skipped, and blocked shell usability checks in reviewable validation artifacts or notes. Environment-dependent validation MAY be skipped only with an explicit record of missing `uv`, xmake, `x86_64-elf-*` toolchain, QEMU, Bochs, ROM/display setup, disk image configuration, serial oracle, timeout controls, or another required local dependency; skipped checks MUST NOT be reported as passed.

#### Scenario: 环境可用时记录执行结果

- **WHEN** shell usability runtime validation completes in a configured environment
- **THEN** the validation artifact or notes MUST record the selected case, configured switches, emulator backend, observed output or markers, log paths, timeout, and result
- **AND** it MUST identify whether QEMU, Bochs, or substitute checks were used

#### Scenario: 环境不可用时记录跳过

- **WHEN** required toolchain, emulator, display/ROM, disk image, serial oracle, or timeout dependency is unavailable
- **THEN** affected shell usability runtime checks MUST be marked skipped or blocked rather than passed
- **AND** validation notes MUST record substitute source/build/spec checks and remaining shell/userland behavior risk

#### Scenario: Validation preserves low-level contracts

- **WHEN** shell usability validation tooling or notes are added
- **THEN** they MUST NOT change boot layout, linker addresses, disk layout, IDT vectors, syscall vector `0x80`, CR3 switching rules, existing smoke marker strings, or default-off smoke behavior
- **AND** any hardware-behavior residual risk MUST be recorded if emulator cross-validation is unavailable
