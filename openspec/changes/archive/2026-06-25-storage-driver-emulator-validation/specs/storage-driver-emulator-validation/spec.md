## ADDED Requirements

### Requirement: 现代存储仿真器验证保持现有架构与启动基线
BigOS SHALL validate modern storage drivers through the existing x86_64 emulator path without adding a new ISA, changing the Legacy BIOS/MBR/exFAT boot image layout, or making the modern storage device a default boot dependency.

#### Scenario: 验证不新增 ISA
- **WHEN** the modern storage emulator validation is configured or executed
- **THEN** it MUST use the current x86_64 kernel, toolchain, boot image, and emulator path
- **AND** it MUST NOT require RISC-V, ARM, or any other ISA backend

#### Scenario: 默认启动介质保持不变
- **WHEN** the validation helper prepares the boot image for a modern storage validation run
- **THEN** the boot image MUST retain the existing Legacy BIOS/MBR/exFAT layout and ATA-compatible default boot exposure
- **AND** any modern storage device used for validation MUST be configured as an explicit validation device rather than a required boot disk

### Requirement: 仿真器验证覆盖现代后端集成闭环
BigOS SHALL provide a default-off emulator validation path that selects the published modern storage backend through an internal selector and exercises it through the block request layer, page/buffer cache, and writeback path.

#### Scenario: 现代后端可用时执行集成读写
- **WHEN** the expected toolchain, emulator, disk image, modern storage device, and interrupt delivery support are available
- **THEN** validation MUST select the published modern storage backend through an internal kernel path
- **AND** it MUST complete at least one cache-mediated write/read round trip whose success depends on terminal success from the block request layer

#### Scenario: 现代后端不可用时不伪造通过
- **WHEN** the modern storage backend is absent, probe failed, feature negotiation failed, queue initialization failed, or interrupt completion is unavailable
- **THEN** validation MUST record the backend as unavailable or failed
- **AND** it MUST NOT fall back to ATA or another backend while reporting modern storage validation success

#### Scenario: 写回成功依赖请求终态成功
- **WHEN** validation checks cache/writeback behavior on the modern storage backend
- **THEN** dirty state or validation success MUST be cleared only after the relevant block request reaches terminal success
- **AND** timeout, device error, issue failure, or completion rejection MUST be recorded as validation failure for that stage

### Requirement: 仿真器验证结果分层记录
BigOS SHALL record modern storage emulator validation results in a reviewable artifact that distinguishes environment skips, backend publication failures, request-layer failures, cache/writeback failures, timeout/error completion, and default boot regressions.

#### Scenario: 验证通过项被记录
- **WHEN** modern storage emulator validation observes all expected success markers within bounded timeouts
- **THEN** the artifact MUST record the emulator backend, display mode, modern storage device configuration, expected markers, observed markers, serial log paths, and per-stage pass results

#### Scenario: 环境不可用被记录为跳过或阻塞
- **WHEN** xmake, uv, cross-binutils, QEMU, Bochs, ROM/display setup, serial capture, disk image generation, modern storage device support, or interrupt delivery support is unavailable
- **THEN** the artifact MUST mark affected validation items as skipped or blocked rather than passed
- **AND** it MUST record substitute checks and residual risk

#### Scenario: 运行时失败被记录到具体阶段
- **WHEN** validation times out, exits early, panics, misses an expected marker, or observes a failed request/cache/writeback stage
- **THEN** the artifact MUST record the failed stage, missing or failed marker, serial log path, timeout or exit status, and residual risk

### Requirement: 默认启动回归独立于现代存储验证
BigOS SHALL validate that normal boot remains independent of modern storage availability when the modern storage validation path is disabled.

#### Scenario: 默认关闭时仍走现有 userland baseline
- **WHEN** the modern storage validation switch or equivalent default-off path is disabled
- **THEN** normal boot MUST remain able to use the existing ATA/exFAT path to reach the current default userland baseline
- **AND** modern storage availability MUST NOT be required for boot success

#### Scenario: 默认启动回归失败不被现代验证掩盖
- **WHEN** modern storage validation passes but the default boot regression check fails or is skipped
- **THEN** the validation record MUST report the default boot result separately
- **AND** it MUST NOT present the overall storage validation as covering default boot compatibility
