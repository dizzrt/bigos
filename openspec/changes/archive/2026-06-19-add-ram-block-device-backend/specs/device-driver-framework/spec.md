## ADDED Requirements

### Requirement: 同类第二块后端发布
BigOS SHALL allow the bounded device-driver framework to register, probe, and publish a second block-device backend for an internal stable role distinct from the boot disk and persistent writable disk roles. The framework MUST preserve deterministic registration, duplicate detection, probe failure, and published-interface lookup behavior for both existing ATA-backed block devices and the second backend.

#### Scenario: 第二块后端发布成功
- **WHEN** kernel initialization or default-off validation registers a valid RAM block device descriptor and compatible driver descriptor for a distinct internal role
- **THEN** the device framework MUST probe and publish that backend without replacing the boot disk or persistent writable disk entries
- **AND** lookup by the RAM block role MUST return the RAM block backend interface

#### Scenario: 同类不同角色相互隔离
- **WHEN** the framework has published both an ATA-backed block device and the RAM block backend
- **THEN** lookup for each stable role MUST return the matching backend interface
- **AND** failure, not-ready state, or queue pressure for one role MUST NOT cause lookup for the other role to return the wrong backend

#### Scenario: 第二后端 probe 失败不影响现有后端
- **WHEN** the RAM block backend probe fails because its descriptor, capacity, or backing storage is invalid
- **THEN** the framework MUST record deterministic failure for that RAM block device
- **AND** existing published ATA-backed block devices MUST remain discoverable through their original roles

### Requirement: 第二后端角色保持内核内部
BigOS SHALL keep any stable role or identifier used for the RAM block backend as a kernel-internal device framework detail. The role MUST NOT become a user-visible syscall ABI, filesystem-visible device numbering contract, disk-layout contract, or external validation-tool ABI.

#### Scenario: 用户态不可观察 RAM 后端角色
- **WHEN** the RAM block backend is registered for framework validation
- **THEN** user programs and filesystem paths MUST NOT gain a new device node, syscall-visible device id, or persistent mount name solely because of that backend
- **AND** validation MUST select the backend through kernel-internal framework lookup rather than a user-visible ABI

#### Scenario: 默认启动边界稳定
- **WHEN** BigOS boots normally with RAM block backend code compiled in but validation disabled
- **THEN** the device framework MUST preserve existing user-visible boot, shell, filesystem, and persistent `/rw` behavior
- **AND** it MUST NOT require a RAM block backend to mount the boot filesystem or start userland
