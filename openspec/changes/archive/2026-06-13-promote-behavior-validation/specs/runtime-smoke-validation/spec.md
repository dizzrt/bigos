## ADDED Requirements

### Requirement: 行为导向验证矩阵覆盖最小可用系统

BigOS runtime validation SHALL provide a behavior-oriented validation matrix for the minimal usable system. The matrix MUST cover representative runtime-observable checks for interactive shell behavior, simple C programs, process/fd semantics, runtime filesystem operations, and bounded userland compatibility. Each behavior check MUST identify the exercised capability, expected observable result, failure signal, and validation layer.

#### Scenario: 默认 userland 行为可观察

- **WHEN** behavior-oriented validation runs the default init and shell path in a configured QEMU headless environment
- **THEN** validation MUST observe that userland reaches the documented shell or init behavior through deterministic serial/log output, command output, exit status, or an equivalent low-level signal
- **AND** missing expected observations MUST be recorded as failure rather than success

#### Scenario: shell 和简单 C 程序行为可判定

- **WHEN** behavior-oriented validation exercises supported shell commands and packaged simple C programs
- **THEN** validation MUST assert expected stdout/stderr, exit status, argument handoff, environment handling, and shell continuation behavior
- **AND** the assertions MUST remain within the bounded shell and minimal libc subset documented for BigOS

#### Scenario: runtime filesystem 行为可复现

- **WHEN** behavior-oriented validation exercises supported runtime filesystem operations
- **THEN** validation MUST assert observable file creation, read, write, seek, sync, directory, or removal effects through file contents, return values, error paths, or command output
- **AND** validation MUST NOT imply support for persistent full writable filesystems, broad file-backed mapping, async I/O, or broad storage/device support

### Requirement: 行为验证保持分层且记录环境缺失

BigOS SHALL keep behavior-oriented validation layered across source/spec consistency, build and packaging checks, QEMU headless runtime assertions, and optional graphical, Bochs, manual input, or hardware-adjacent evidence. Environment-dependent validation MAY be skipped only when the result records the missing dependency, substitute checks, and residual risk.

#### Scenario: 环境依赖检查缺失时不冒充通过

- **WHEN** QEMU, Bochs, the x86_64 cross-toolchain, display/ROM dependencies, serial logging, keyboard input, or disk image configuration are unavailable
- **THEN** the corresponding behavior validation MUST be marked skipped or blocked instead of passed
- **AND** the validation record MUST identify the unavailable dependency, substitute checks that were run, and the remaining risk

#### Scenario: 场景化低层交叉验证保持可选

- **WHEN** a change affects boot, IRQ, timer, ATA PIO, port IO, display input, or other hardware-sensitive behavior
- **THEN** Bochs or QEMU plus Bochs cross-validation SHOULD be recorded when available
- **AND** absence of that cross-validation MUST NOT block unrelated source-only or documentation-only changes when substitute checks and residual risk are recorded

#### Scenario: 行为验证不扩大运行时边界

- **WHEN** behavior-oriented validation is added, documented, or executed
- **THEN** it MUST NOT require UEFI, OVMF, ESP/FAT images, virtio, AHCI/SATA, NVMe, SMP, dynamic linking, full POSIX terminal support, job control, or a complete POSIX libc
- **AND** it MUST NOT change boot layout, kernel link addresses, IDT vectors, syscall vector `0x80`, CR3 switching rules, disk layout, or existing ABI assumptions
