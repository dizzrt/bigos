## ADDED Requirements

### Requirement: 持久可写卷识别与挂载
BigOS SHALL provide a bounded persistent writable filesystem volume for the current x86_64 Legacy BIOS storage path using a build-tool-created independent persistent test disk. The persistent volume MUST be identified by explicit on-disk metadata including magic, current format version, block size, capacity limits, and root metadata, and MUST be mounted as the writable `/rw` backend only after all recognition checks pass. This capability MUST NOT modify the MBR, exFAT boot assets, packaged user binaries, boot handoff ABI, page-table layout, interrupt vectors, or syscall ABI.

#### Scenario: 识别既有持久卷并挂载为 `/rw`
- **WHEN** the kernel initializes filesystem support and finds a compatible persistent writable volume on the independent persistent test disk
- **THEN** BigOS MUST mount that volume as `/rw`
- **AND** existing read-only exFAT boot assets MUST remain readable and isolated from `/rw` mutations

#### Scenario: 不兼容卷拒绝挂载
- **WHEN** the independent persistent test disk has an invalid magic, old version, unknown version, future version, invalid block size, invalid capacity bounds, or inconsistent root metadata
- **THEN** BigOS MUST reject the persistent mount with a deterministic diagnostic or error path
- **AND** it MUST NOT overwrite the volume, auto-migrate the volume, auto-format the volume, modify read-only exFAT state, or panic during normal initialization

### Requirement: 显式格式化新持久卷
BigOS SHALL support an explicit controlled formatting path for an empty persistent writable volume through a minimal user-space mkfs tool. The mkfs tool MUST remain bounded and MUST NOT imply a complete POSIX mount/format toolchain. Formatting MUST initialize the superblock, free-space metadata, root directory, inode metadata, and required data blocks before publishing the volume as `/rw`. Normal boot MUST NOT silently format a persistent area merely because recognition failed.

#### Scenario: 显式格式化后发布挂载
- **WHEN** the minimal user-space mkfs tool or default-off validation path requests formatting of an empty supported independent persistent test disk
- **THEN** BigOS MUST initialize a complete bounded persistent filesystem image
- **AND** it MUST publish `/rw` only after all required metadata writes and verification reads succeed

#### Scenario: mkfs 工具保持有界
- **WHEN** the minimal mkfs tool is used to format the persistent test disk
- **THEN** it MUST only expose the bounded formatting behavior needed by BigOS persistent `/rw`
- **AND** it MUST NOT claim complete POSIX `mkfs`, device management, partition editing, or mount tooling compatibility

#### Scenario: 普通挂载失败不自动格式化
- **WHEN** normal boot attempts to mount a persistent volume and recognition fails
- **THEN** BigOS MUST NOT automatically format the area
- **AND** it MUST either fall back to the RAM-backed `/rw` mode or leave persistent `/rw` unpublished according to the configured policy

### Requirement: 同步后的状态跨 clean reboot 可见
BigOS SHALL make successfully synchronized persistent `/rw` file data, directory entries, and bounded metadata visible after a clean reboot using the same independent persistent test disk image. The persistence guarantee MUST apply to states committed by successful `fsync`, explicit sync, or cache write-back before the clean shutdown or validation reboot boundary. This requirement MUST NOT claim crash consistency, journal replay, power-loss safety, or recovery from partially completed metadata updates after an unclean stop.

#### Scenario: 写入同步后第二次启动读回
- **WHEN** a user process creates a file under persistent `/rw`, writes bounded content, calls `fsync` successfully, and the validation script starts the emulator a second time with the same independent persistent test disk image
- **THEN** BigOS MUST remount the persistent `/rw` volume
- **AND** reopening the file MUST return the synchronized content and bounded metadata

#### Scenario: 未同步 dirty 数据不扩大承诺
- **WHEN** a user process writes data under persistent `/rw` but the dirty blocks are not successfully synchronized before reboot
- **THEN** BigOS MUST NOT claim that the unsynchronized data persists
- **AND** previously synchronized filesystem state MUST remain explainable within the non-journaled clean-sync boundary

### Requirement: 持久后端失败保持可解释状态
BigOS SHALL return deterministic negative errno values or documented diagnostics for persistent `/rw` failures including capacity exhaustion, allocation failure, block I/O failure, incompatible volume metadata, unsupported object types, invalid paths, permission denial, and write-back failure. Failed operations MUST NOT publish partially initialized files, directories, rename targets, truncated metadata, dirty-cache success, or corrupted read-only exFAT state.

#### Scenario: 容量耗尽不发布半成品
- **WHEN** persistent `/rw` create, write, mkdir, rename, truncate, or metadata update requires an inode, directory slot, data block, cache block, or kernel allocation that is unavailable
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** later lookup, read, metadata query, and directory enumeration MUST observe the pre-failure state

#### Scenario: 块写失败不静默成功
- **WHEN** persistent `/rw` `fsync`, sync, or cache eviction cannot write required blocks to the underlying block device
- **THEN** BigOS MUST report a deterministic write-back error
- **AND** it MUST NOT silently discard dirty data or mark the affected persistent state as successfully committed

### Requirement: 持久文件系统验证可复现
BigOS SHALL provide default-off validation that exercises persistent writable filesystem behavior under the current x86_64 Legacy BIOS emulator path. Validation MUST record toolchain, emulator, ROM/display, and disk-image availability; passed checks; skipped checks with reasons; and residual risk.

#### Scenario: 双启动 smoke validates persistence
- **WHEN** persistent filesystem validation is enabled and the required xmake, x86_64-elf toolchain, emulator, and writable disk image are available
- **THEN** validation MUST use one script to launch the emulator twice with the same independent persistent test disk image: the first launch formats or mounts the volume, creates and synchronizes bounded files and directories, and emits a write-stage result; the second launch remounts `/rw`, verifies the synchronized state, and emits a verify-stage result
- **AND** it MUST verify that read-only exFAT boot assets remain readable

#### Scenario: unavailable emulator or toolchain is recorded
- **WHEN** QEMU, Bochs, the cross toolchain, ROM/display support, or the required disk image path is unavailable
- **THEN** validation MUST be recorded as skipped or blocked with the missing dependency and residual risk
- **AND** it MUST NOT be reported as runtime-passed
