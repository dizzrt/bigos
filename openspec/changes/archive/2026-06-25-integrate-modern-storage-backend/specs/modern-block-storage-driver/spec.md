## ADDED Requirements

### Requirement: 现代存储后端可被块层显式选择
BigOS SHALL allow a successfully published modern block-storage backend to be selected by kernel-internal block-layer consumers through a stable explicit role or equivalent internal selector. The selector MUST remain kernel-internal and MUST NOT create a user-visible device node, syscall ABI, mount name, or default boot dependency.

#### Scenario: 显式选择已发布现代后端
- **WHEN** the modern block-storage driver has successfully published a ready backend and a default-off validation or internal kernel path requests that explicit backend
- **THEN** BigOS MUST return the published backend's block interface to the caller
- **AND** the returned backend MUST be usable through the ordinary block I/O request layer

#### Scenario: 未发布现代后端不可被伪造
- **WHEN** the modern block-storage backend is absent, probe failed, feature negotiation failed, or queue initialization failed
- **THEN** BigOS MUST return a deterministic not-found, not-ready, or equivalent diagnostic result
- **AND** it MUST NOT construct an unrelated fallback backend under the modern storage selector

#### Scenario: 默认启动不依赖现代后端选择
- **WHEN** the explicit modern-storage validation path is not enabled
- **THEN** BigOS MUST keep the existing default boot, exFAT, ATA, `/rw`, and userland baseline behavior independent of modern backend availability
- **AND** the modern backend MUST NOT become a required device for default boot success

### Requirement: 现代后端集成验证覆盖块层和写回路径
BigOS SHALL provide default-off validation that exercises the published modern block-storage backend through the block request layer, page/buffer cache, and writeback path. The validation MUST distinguish backend publication failures, request-layer failures, cache/writeback failures, and environment skips.

#### Scenario: 集成验证读写往返
- **WHEN** the toolchain, emulator, disk image, and modern storage device configuration are available and the default-off integration validation is enabled
- **THEN** validation MUST select the modern backend, submit read and write operations through the request layer, and verify a cache-mediated write/read round trip
- **AND** success MUST be reported only after the request layer observes terminal success for the relevant operations

#### Scenario: 集成验证记录环境不可用
- **WHEN** the required cross toolchain, emulator support, serial capture, disk image, modern storage device, MSI-X delivery, or backend configuration is unavailable
- **THEN** validation MUST record the skipped or blocked integration coverage and residual risk
- **AND** it MUST NOT report modern-storage integration runtime success for the skipped environment
