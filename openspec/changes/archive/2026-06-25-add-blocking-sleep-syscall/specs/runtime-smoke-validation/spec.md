## ADDED Requirements

### Requirement: runtime smoke validates blocking sleep syscall
BigOS runtime smoke validation SHALL include a default-off blocking sleep syscall case that validates the user-visible sleep wrapper through monotonic tick observations and deterministic serial markers. The smoke MUST remain narrow, MUST NOT enable unrelated smoke switches unless required by the userland execution path, and MUST record unavailable runtime dependencies as skipped or blocked rather than passed.

#### Scenario: matrix lists sleep syscall case
- **WHEN** a developer inspects the runtime smoke matrix after this change
- **THEN** the matrix MUST list a blocking sleep syscall case with its xmake switch, expected marker, preferred emulator path, timeout, and generated serial log path
- **AND** the case MUST remain disabled in ordinary builds unless explicitly selected

#### Scenario: QEMU headless observes sleep marker
- **WHEN** the sleep syscall smoke runs through the supported QEMU headless marker-check flow
- **THEN** the runner MUST configure the required smoke switch, build the kernel and user payloads, boot the generated image, and wait for the sleep syscall success marker within a bounded timeout
- **AND** the result MUST record the serial log path and observed or missing marker

#### Scenario: sleep smoke validates tick lower bound
- **WHEN** the sleep syscall smoke user program executes
- **THEN** it MUST read the monotonic tick before and after a bounded sleep request
- **AND** it MUST fail if the observed tick delta is below the expected tick lower bound
- **AND** it MUST NOT require precise wall-clock timing, GUI display, hosted OS sleep, or high-resolution timers

#### Scenario: runtime 依赖缺失显式记录
- **WHEN** required runtime tooling such as `uv`, xmake, `x86_64-elf-*`, QEMU, Bochs, ROM/display configuration, generated image paths, or serial logging is unavailable
- **THEN** the validation artifact MUST record the missing dependency, skipped sleep case, substitute checks, and residual syscall/timer/scheduler risk
- **AND** it MUST NOT report blocking sleep runtime validation as passed
