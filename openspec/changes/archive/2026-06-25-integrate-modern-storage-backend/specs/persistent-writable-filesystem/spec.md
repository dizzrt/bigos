## ADDED Requirements

### Requirement: 持久 clean-sync 可显式使用现代存储后端
BigOS SHALL allow persistent clean-sync `/rw` validation or an equivalent explicit internal configuration to use a selected modern block-storage backend as its backing block device. This MUST remain opt-in and MUST NOT replace the default boot, default exFAT, default ATA, or default `/rw` backend policy.

#### Scenario: 显式现代后端挂载持久卷
- **WHEN** persistent `/rw` validation explicitly selects a ready modern block-storage backend containing a compatible persistent volume
- **THEN** BigOS MUST mount or validate that volume through the existing persistent clean-sync filesystem path
- **AND** all backing I/O MUST flow through the block request layer and page/buffer cache rather than modern-driver private filesystem hooks

#### Scenario: 默认路径不自动改用现代后端
- **WHEN** normal boot runs without the explicit modern-backend persistent validation or configuration
- **THEN** BigOS MUST keep the existing default boot and `/rw` backend behavior
- **AND** it MUST NOT silently mount a modern-backend volume as `/rw` merely because the device is present

### Requirement: 现代后端持久同步经缓存写回后才成功
Persistent `/rw` clean-sync success on a modern block-storage backend SHALL depend on successful page/buffer cache writeback of the required dirty file data and filesystem metadata blocks to that backend. `fsync`, explicit sync, eviction writeback, or a clean validation boundary MUST NOT report durable success until the selected writes reach terminal success through the request layer.

#### Scenario: fsync 成功后现代后端 clean reboot 读回
- **WHEN** a process writes bounded data or metadata under persistent `/rw` backed by the explicitly selected modern backend and `fsync` returns success
- **THEN** BigOS MUST have written the required dirty cache blocks to the modern backend through the request layer before returning success
- **AND** a later clean validation reboot using the same modern-backend disk image MUST observe the synchronized content and metadata

#### Scenario: 现代后端写回失败不声明持久成功
- **WHEN** persistent `/rw` synchronization on the modern backend encounters request rejection, queue exhaustion, issue failure, timeout, completion rejection, or device error
- **THEN** BigOS MUST return a deterministic synchronization or writeback error
- **AND** it MUST NOT mark the affected persistent state as durably committed or clear the required dirty state as though it were written

### Requirement: 现代后端持久验证保持 clean-sync 边界
BigOS SHALL provide default-off validation for persistent `/rw` on the explicitly selected modern backend that records clean-sync behavior and environment availability. The validation MUST NOT claim crash consistency, journal replay, power-loss safety, default storage replacement, or persistence for unsynchronized dirty state.

#### Scenario: 双阶段验证现代后端持久状态
- **WHEN** the required toolchain, emulator, serial capture, modern storage device configuration, and persistent disk image are available
- **THEN** validation MUST run a clean-sync flow that writes bounded `/rw` data or metadata, synchronizes it, and verifies it after a clean reboot or equivalent remount using the same modern-backend image
- **AND** it MUST report success only for state synchronized through cache writeback and request-layer terminal success

#### Scenario: 未同步 dirty 状态不扩大承诺
- **WHEN** persistent `/rw` backed by the modern backend contains dirty cache blocks that have not synchronized successfully before the clean validation boundary
- **THEN** BigOS MUST NOT describe those blocks as durable
- **AND** previously synchronized persistent state MUST remain explainable within the bounded clean-sync contract

#### Scenario: 环境不可用时记录跳过
- **WHEN** the modern-backend persistent validation cannot run because the cross toolchain, emulator, serial capture, device configuration, MSI-X delivery, disk image, or volume metadata is unavailable
- **THEN** validation notes MUST record the missing dependency and residual risk as skipped or blocked
- **AND** they MUST NOT report runtime persistent validation as passed
