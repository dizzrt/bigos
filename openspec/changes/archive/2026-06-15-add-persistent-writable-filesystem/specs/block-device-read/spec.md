## ADDED Requirements

### Requirement: 持久文件系统可依赖块设备写入能力
BigOS SHALL expose block device write behavior sufficient for the persistent writable filesystem backend to write whole sectors to a configured writable storage area. Writable devices MUST validate sector count, source buffer size, and LBA arithmetic before issuing I/O. Read-only devices MUST reject writes deterministically without issuing device writes. The write path MUST preserve the existing read path, sector-size contract, x86_64 Legacy BIOS boot flow, and MBR/exFAT discovery contract.

#### Scenario: 持久后端整扇区写成功
- **WHEN** the persistent filesystem requests a whole-sector write to a writable block device with a valid source buffer and non-overflowing LBA range
- **THEN** the block device layer MUST write the requested sectors and return success
- **AND** a later block read of the same sectors MUST return the written contents unless a later successful write changed them

#### Scenario: 只读设备拒绝持久写
- **WHEN** the persistent filesystem or cache write-back path attempts to write to a block device without a write implementation
- **THEN** the block device layer MUST return a deterministic unsupported or read-only status
- **AND** it MUST NOT issue hardware writes or modify read-only exFAT boot assets

#### Scenario: LBA 或缓冲区校验失败不发起 IO
- **WHEN** a write request has an overflowing LBA range, zero or invalid sector count, or a source buffer smaller than the required byte count
- **THEN** the block device layer MUST reject the request before issuing device I/O
- **AND** the persistent filesystem MUST observe a deterministic failure

### Requirement: 同步块写上下文边界
Block device writes used by the persistent writable filesystem SHALL be callable only from ordinary kernel context after port I/O and memory management are initialized. The write API MUST NOT be advertised as IRQ-handler-safe, asynchronous, preemption-safe, or safe from scheduler critical sections.

#### Scenario: 普通上下文执行持久写
- **WHEN** page/buffer cache write-back for persistent `/rw` calls the block write API from an allowed blocking context
- **THEN** the block device layer MAY perform synchronous polling I/O and return an explicit status

#### Scenario: IRQ 上下文写不受支持
- **WHEN** code attempts to treat persistent block writes as IRQ-handler-safe or preemption-disabled-safe behavior
- **THEN** the specification does not guarantee correctness
- **AND** implementation documentation or diagnostics MUST mark this usage unsupported

### Requirement: 设备写错误确定性上报
Block device write backends used by persistent `/rw` SHALL report polling timeout, device error, flush failure, unsupported address mode, and hardware-not-ready states through deterministic status values. The backend MUST NOT spin forever and MUST NOT silently report success after a failed write.

#### Scenario: ATA PIO 写超时
- **WHEN** an ATA PIO write or flush operation fails to reach the expected device state within the bounded polling limit
- **THEN** the backend MUST stop polling and return a timeout or device error status
- **AND** the cache and filesystem layers MUST be able to map that status to a deterministic write-back failure

#### Scenario: 失败写不破坏后续读契约
- **WHEN** a block write backend reports failure
- **THEN** the block device layer MUST preserve the existing sector-size and read API contract
- **AND** later reads MUST either return the previously committed device contents or a deterministic device error
