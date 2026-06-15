## ADDED Requirements

### Requirement: `/rw` 可选择持久承载
BigOS SHALL allow the writable `/rw` backend to be backed either by the existing RAM-backed runtime filesystem or by a recognized persistent writable volume. The selected backend MUST preserve the existing bounded create, open, read, write, lseek, fsync, mkdir, unlink, restricted regular-file rename, metadata, permission, and directory enumeration contracts. The RAM-backed mode MUST remain explicitly non-persistent; the persistent mode MUST only claim cross-reboot visibility for successfully synchronized state.

#### Scenario: RAM-backed 模式仍不承诺持久化
- **WHEN** `/rw` is mounted using the RAM-backed backend
- **THEN** successful writes and metadata updates MUST remain visible within the current boot session
- **AND** documentation, specs, tools, and validation MUST NOT claim that RAM-backed `/rw` state survives reboot

#### Scenario: 持久模式复用现有 `/rw` 操作语义
- **WHEN** `/rw` is mounted using a recognized persistent writable volume
- **THEN** existing bounded file, directory, metadata, permission, fd-reference, and restricted rename semantics MUST apply to the persistent backend
- **AND** successfully synchronized state MUST be eligible for clean-reboot visibility according to the persistent filesystem contract

### Requirement: `/rw` 后端选择失败不破坏只读资产
BigOS SHALL keep read-only exFAT boot assets isolated from `/rw` backend selection, formatting, mounting, synchronization, and failure paths. Persistent `/rw` mount failure MUST NOT modify the exFAT boot image, packaged user programs, kernel image, MBR, or bootloader data. If fallback to RAM-backed `/rw` is configured, fallback MUST be explicit in diagnostics or validation records.

#### Scenario: 持久挂载失败后 exFAT 仍可读
- **WHEN** persistent `/rw` mount fails because the volume is absent, incompatible, corrupt, or unavailable
- **THEN** BigOS MUST preserve read-only exFAT discovery and reads
- **AND** it MUST either expose the configured RAM-backed `/rw` fallback or report that no persistent writable mount was published

#### Scenario: 格式化不会覆盖 boot assets
- **WHEN** an explicit persistent `/rw` formatting path is executed
- **THEN** formatting MUST be limited to the configured persistent area
- **AND** it MUST NOT overwrite MBR sectors, exFAT metadata, kernel files, user binaries, or other boot assets
