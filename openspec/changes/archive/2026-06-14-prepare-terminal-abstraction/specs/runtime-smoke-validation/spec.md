## ADDED Requirements

### Requirement: Terminal preparation validation remains layered
BigOS SHALL validate minimal terminal abstraction work through layered source/spec, build, headless runtime, and optional interactive evidence. Automated validation MUST prefer deterministic QEMU headless serial/log checks for default userland reachability and behavior-oriented assertions where available, while graphical display, manual keyboard input, emulator scancode injection, and Bochs cross-validation remain environment-dependent evidence that MUST be recorded separately.

#### Scenario: Headless validation preserves default userland checks
- **WHEN** terminal preparation changes are validated in a configured QEMU headless environment
- **THEN** validation MUST continue to observe default init/shell or userland behavior through deterministic serial/log output, command output, exit status, or another low-level signal
- **AND** missing expected observations MUST be recorded as failure rather than success

#### Scenario: Interactive evidence is optional and explicit
- **WHEN** graphical QEMU, Bochs, manual keyboard input, emulator keyboard injection, display/ROM setup, and disk image generation are available
- **THEN** validation SHOULD record evidence that prompt text, bounded input feedback, control-character behavior, and command output are visible on the runtime text console
- **AND** the evidence MUST identify emulator backend, display/input method, executed input sequence, observed output, result, and any residual risk

#### Scenario: Missing interactive environment is not a pass
- **WHEN** local display, keyboard input, emulator injection, Bochs ROM/display, QEMU, cross-toolchain, serial logging, disk image generation, or `uv` support is unavailable
- **THEN** affected interactive terminal validation MUST be marked skipped or blocked rather than passed
- **AND** validation notes MUST record substitute source/build/headless checks and remaining terminal behavior risk

### Requirement: Terminal validation preserves low-level contracts
BigOS SHALL keep terminal abstraction validation within the current bounded x86_64 Legacy BIOS runtime boundary and existing behavior-oriented validation model. Validation MUST NOT require new boot backends, new storage drivers, SMP, full POSIX terminal behavior, job control, process groups, dynamic linking, hosted libc, or changes to existing smoke defaults and marker semantics.

#### Scenario: Runtime contracts are unchanged
- **WHEN** terminal validation tooling, docs, or review notes are added
- **THEN** they MUST NOT change boot layout, linker addresses, disk layout, IDT vectors, syscall vector `0x80`, CR3 switching rules, existing smoke marker strings, default-off smoke behavior, or user/kernel ABI assumptions
- **AND** if implementation touches IRQ, keyboard, console, port IO, or hardware-adjacent behavior, validation notes MUST recommend Bochs or QEMU+Bochs cross-validation when local environment supports it

#### Scenario: Validation record separates results
- **WHEN** terminal preparation validation is reported
- **THEN** the record MUST distinguish passed checks, skipped or blocked checks, historical diagnostics, current-change diagnostics, substitute checks, and residual risks
- **AND** Python-related helper execution, if any, MUST be described with `uv run ...`; if `uv` is unavailable, the record MUST state that blocker explicitly
