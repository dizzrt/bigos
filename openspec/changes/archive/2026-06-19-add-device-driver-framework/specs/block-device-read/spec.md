## ADDED Requirements

### Requirement: 块设备通过设备框架发布
BigOS SHALL publish existing synchronous block devices through the kernel device-driver framework before filesystem consumers use them. The framework-published block interface MUST preserve the existing `BlockDevice` read/write validation, sector-size contract, deterministic status values, and synchronous non-IRQ context boundary.

#### Scenario: boot disk 经框架查找后读取
- **WHEN** the VFS or filesystem mount path needs the current boot disk block device
- **THEN** BigOS MUST resolve a published block device for the boot disk role through the device framework
- **AND** reads through the returned block interface MUST preserve the existing whole-sector read validation and error behavior

#### Scenario: persistent disk 经框架查找后写入
- **WHEN** the persistent writable backend is enabled and needs its configured backing block device
- **THEN** BigOS MUST resolve a published writable block device for the persistent writable role through the device framework
- **AND** writes through the returned block interface MUST preserve the existing whole-sector write validation, read-only rejection, flush/error behavior, and clean-sync assumptions

#### Scenario: 未发布块设备不被文件系统使用
- **WHEN** no probed and published block device exists for the requested filesystem role
- **THEN** BigOS MUST return a deterministic initialization or lookup error to the caller
- **AND** the filesystem MUST NOT directly construct an unrelated hardware backend to bypass the framework

### Requirement: ATA PIO backend 接入框架不改变硬件契约
BigOS SHALL adapt the current ATA PIO block backend to the device-driver framework without changing its hardware-visible contract. The backend MUST retain its bounded polling, 512-byte sector interface, supported LBA mode, deterministic timeout/error statuses, and non-IRQ context limitation.

#### Scenario: ATA PIO probe 发布 BlockDevice
- **WHEN** the framework probes the ATA PIO descriptor for a supported configured role
- **THEN** the ATA PIO driver MUST initialize the existing `BlockDevice` fields and publish that interface through the framework
- **AND** subsequent block reads or writes MUST use the same backend validation and polling behavior as before framework integration

#### Scenario: ATA PIO probe 失败
- **WHEN** ATA PIO probe cannot initialize the configured device role or detects an unsupported hardware state
- **THEN** the framework MUST record a deterministic probe failure
- **AND** consumers MUST NOT receive a usable `BlockDevice` for that failed role

### Requirement: 块设备 consumer 不依赖具体 ATA 初始化
BigOS SHALL keep filesystem and cache consumers dependent on the block-device contract, not on concrete ATA PIO initialization helpers. Consumers MAY retain fallback behavior that is already part of their bounded contract, but MUST NOT silently bypass the device framework for hardware-backed block devices once framework integration is complete.

#### Scenario: VFS 不直接初始化 boot ATA
- **WHEN** the read-only boot filesystem initializes after the device framework has probed devices
- **THEN** it MUST obtain its boot disk block interface from the framework
- **AND** it MUST NOT directly call the ATA PIO boot-disk initialization helper as its normal path

#### Scenario: persistent `/rw` 保留 RAM fallback 边界
- **WHEN** the persistent writable block device is unavailable or probe fails
- **THEN** bigfs MAY retain its existing RAM-backed runtime storage fallback according to the current bounded `/rw` contract
- **AND** it MUST NOT report persistent clean-sync success for an unavailable or failed hardware-backed device
