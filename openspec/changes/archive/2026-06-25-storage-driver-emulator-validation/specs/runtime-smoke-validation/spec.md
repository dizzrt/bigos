## ADDED Requirements

### Requirement: Runtime smoke 矩阵覆盖现代存储仿真器验证
BigOS SHALL extend the runtime smoke validation matrix with a default-off modern storage emulator validation case that lists its switches, emulator requirements, expected serial markers, bounded timeout, generated logs, and artifact fields.

#### Scenario: 矩阵列出现代存储验证用例
- **WHEN** a developer inspects the runtime smoke validation matrix
- **THEN** the matrix MUST include a modern storage emulator validation case
- **AND** the case MUST list the required default-off configuration, preferred QEMU headless path, expected pass/fail markers, case-specific timeout, serial log path, and structured artifact output path

#### Scenario: 现代存储验证保持默认关闭
- **WHEN** BigOS is built or booted outside the explicit modern storage validation case
- **THEN** the modern storage validation switch or equivalent validation path MUST remain disabled
- **AND** existing memory, timer, scheduler, syscall, filesystem, blocking, userland, and default init smoke defaults MUST remain unchanged

### Requirement: Runtime smoke runner 配置附加现代存储设备
BigOS SHALL configure the emulator for modern storage validation by attaching the modern storage device needed by the validation case while preserving the existing boot image and default boot disk semantics.

#### Scenario: Runner 启动现代存储验证
- **WHEN** the runtime smoke runner executes the modern storage validation case
- **THEN** it MUST build the configured kernel through xmake and launch the preferred emulator with the existing generated boot image
- **AND** it MUST attach the modern storage validation device explicitly without requiring that device for bootloader or root filesystem loading

#### Scenario: Runner 记录设备配置
- **WHEN** the modern storage validation case is executed, skipped, or blocked
- **THEN** the validation artifact MUST record the requested emulator backend, display mode, modern storage device configuration, boot image path category, serial log path, and detected support status

#### Scenario: Runner 不把跳过解释为通过
- **WHEN** emulator support, modern storage device configuration, interrupt delivery, serial capture, or disk image generation is unavailable
- **THEN** the runtime smoke runner MUST mark the modern storage validation case as skipped or blocked
- **AND** it MUST NOT report the case as passed based only on source-level checks or default boot success

### Requirement: Runtime smoke artifact 记录现代存储阶段结果
BigOS SHALL record modern storage validation as staged results in the runtime smoke artifact, including backend publication, request completion, cache/writeback round trip, default boot regression, and skipped cross-validation.

#### Scenario: 阶段结果可审查
- **WHEN** the modern storage validation case completes
- **THEN** the artifact MUST include separate status fields or equivalent reviewable entries for backend publication, block request completion, cache/writeback round trip, default boot regression, and emulator cross-validation
- **AND** each failed or skipped stage MUST include a reason and residual risk

#### Scenario: 默认启动回归单独记录
- **WHEN** the runtime smoke runner records modern storage validation
- **THEN** it MUST record whether normal boot without the modern storage validation path was executed, skipped, or blocked
- **AND** it MUST keep that result separate from the modern backend read/write result
