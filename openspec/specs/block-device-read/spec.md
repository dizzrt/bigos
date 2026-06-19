## Purpose

Define the first kernel-only block device read capability for BigOS: a bounded
whole-sector read API, a synchronous ATA PIO read-only backend for the current
Bochs raw disk setup, explicit non-IRQ context limits, and deterministic smoke
diagnostics. This capability is intentionally read-only and does not introduce
async IO, request queues, write support, caching, partition management, or
filesystem semantics.
## Requirements
### Requirement: Kernel block device read API

BigOS SHALL provide a kernel-only block device read execution interface that reads whole sectors from an LBA address into a caller-provided kernel buffer. The API MUST validate sector count, destination buffer length, and arithmetic overflow before issuing device I/O. After the block I/O request layer is available, ordinary kernel storage consumers MUST submit reads through the request layer, while the block device read API remains the lower-level execution interface used by the request layer and tightly bounded hardware diagnostics.

#### Scenario: Aligned sector read succeeds

- **WHEN** the block I/O request layer dispatches one or more sectors from a registered readable block device with a destination buffer large enough for `sector_count * sector_size`
- **THEN** the block device API reads the requested sectors into the destination buffer and returns success.

#### Scenario: Buffer is too small

- **WHEN** kernel code requests sectors with a destination buffer smaller than the required byte count
- **THEN** the block device API rejects the request before issuing device I/O and returns a bounded error status.

#### Scenario: LBA arithmetic overflows

- **WHEN** kernel code requests a sector range whose end LBA calculation overflows the address type
- **THEN** the block device API rejects the request before issuing device I/O and returns a bounded error status.

#### Scenario: 普通消费者经请求层读取

- **WHEN** page/buffer cache or a filesystem-backed normal storage consumer needs to read from a published block device after request-layer initialization
- **THEN** it MUST submit the read through the block I/O request layer
- **AND** it MUST NOT bypass request queueing by directly invoking the low-level read execution interface as its normal path

### Requirement: ATA PIO read backend

BigOS SHALL provide an ATA PIO read-only block backend for the current Bochs raw disk setup. The backend MUST document its hardware limits, including synchronous polling, 512-byte sectors, primary-master addressing, and the supported LBA mode.

#### Scenario: ATA PIO backend reads a known sector

- **WHEN** the FS smoke opens the ATA PIO block backend against the Bochs raw image
- **THEN** the backend can read a known sector used by the test image and report success to the caller.

#### Scenario: ATA device is not ready

- **WHEN** the ATA PIO status polling does not reach the expected ready/data state within the bounded timeout
- **THEN** the backend stops polling and returns a device timeout or device error status without hanging the kernel indefinitely.

### Requirement: Block IO context boundary

Block device reads SHALL be callable only from ordinary kernel context after port I/O and memory management are initialized. The API MUST NOT be advertised as IRQ-handler-safe, preemption-safe, asynchronous, or sleepable.

#### Scenario: Non-IRQ context read

- **WHEN** kernel initialization or a smoke test calls the block read API after memory initialization
- **THEN** the call is within the supported context boundary and may perform synchronous polling I/O.

#### Scenario: IRQ context is out of scope

- **WHEN** code attempts to treat the block read API as IRQ-handler-safe behavior
- **THEN** the specification does not guarantee correctness, and implementation documentation MUST mark this usage unsupported.

### Requirement: Block read diagnostics

Block read failures SHALL be reported through explicit status values and deterministic diagnostic markers in smoke paths. The driver MUST NOT silently ignore short reads, device errors, buffer validation failures, or polling timeouts.

#### Scenario: Smoke observes a read failure

- **WHEN** the block backend fails during the filesystem smoke path
- **THEN** the smoke emits a deterministic failure marker containing a bounded error code before returning or panicking according to the smoke contract.

#### Scenario: Normal boot keeps block smoke disabled

- **WHEN** the block/filesystem smoke build option is disabled
- **THEN** normal kernel boot does not require ATA PIO probing or disk reads from the new block device layer.

### Requirement: Kernel block device write API

BigOS SHALL 在现有块读接口之上提供一个内核块设备写执行接口，把整扇区从调用方缓冲写入某 LBA 地址。`BlockDevice` MUST 以追加写入口（如 `write_impl`）暴露写能力，`write_impl` 为空的设备 MUST 被视为只读；写 API MUST 在发起设备写前校验扇区数、源缓冲长度与算术溢出，且 MUST NOT 改变现有只读读路径、扇区大小或 MBR/exFAT 发现契约。After the block I/O request layer is available, ordinary kernel storage consumers MUST submit writes through the request layer, while the block device write API remains the lower-level execution interface used by the request layer and tightly bounded hardware diagnostics.

#### Scenario: Aligned sector write succeeds

- **WHEN** the block I/O request layer dispatches a write of one or more sectors to a writable block device with a source buffer large enough for `sector_count * sector_size`
- **THEN** 块设备写 API MUST 把请求扇区写入底层设备并返回成功

#### Scenario: Read-only device rejects write

- **WHEN** 内核对一个 `write_impl` 为空（只读）的块设备请求写扇区
- **THEN** 块设备写 API MUST 返回确定性的 Unsupported 状态（由上层映射为 `-EROFS`），MUST NOT 发起设备写

#### Scenario: Write buffer or LBA validation fails

- **WHEN** 写请求的源缓冲小于所需字节数，或扇区范围的末端 LBA 计算溢出
- **THEN** 块设备写 API MUST 在发起设备 I/O 前拒绝请求并返回有界错误状态

#### Scenario: Device write error is deterministic

- **WHEN** ATA PIO 写在轮询/写数据/flush 阶段未达预期状态或超时
- **THEN** 后端 MUST 停止轮询并返回设备超时或设备错误状态而不无限挂起，且 MUST NOT 破坏后续读路径或扇区大小契约

#### Scenario: 普通消费者经请求层写入

- **WHEN** page/buffer cache, persistent `/rw`, or another normal storage consumer needs to write to a published block device after request-layer initialization
- **THEN** it MUST submit the write through the block I/O request layer
- **AND** it MUST NOT bypass request queueing by directly invoking the low-level write execution interface as its normal path

### Requirement: Block write context boundary

块设备写 SHALL 仅在端口 I/O 与内存管理初始化后的可阻塞普通内核上下文调用。写 API MUST NOT 被标注为 IRQ-handler-safe、preemption-safe、异步或可在不可阻塞上下文调用。

#### Scenario: Non-IRQ context write

- **WHEN** 内核在可阻塞普通上下文（如经块缓冲缓存落盘）调用块写 API
- **THEN** 该调用在支持的上下文边界内，MAY 执行同步轮询写 I/O

#### Scenario: IRQ context write is out of scope

- **WHEN** 代码试图把块写 API 当作 IRQ-handler-safe 行为使用
- **THEN** 规范不保证正确性，实现文档 MUST 标注该用法不支持

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

### Requirement: 块设备通过设备框架发布
BigOS SHALL publish existing synchronous block devices through the kernel device-driver framework before filesystem consumers or the block I/O request layer use them. The framework-published block interface MUST preserve the existing `BlockDevice` read/write validation, sector-size contract, deterministic status values, and synchronous non-IRQ context boundary. The block I/O request layer MUST resolve or receive only published block interfaces for normal dispatch and MUST NOT fabricate hardware-backed devices.

#### Scenario: boot disk 经框架查找后读取
- **WHEN** the VFS, filesystem mount path, or block I/O request layer needs the current boot disk block device
- **THEN** BigOS MUST resolve a published block device for the boot disk role through the device framework
- **AND** reads dispatched through the request layer to the returned block interface MUST preserve the existing whole-sector read validation and error behavior

#### Scenario: persistent disk 经框架查找后写入
- **WHEN** the persistent writable backend is enabled and needs its configured backing block device
- **THEN** BigOS MUST resolve a published writable block device for the persistent writable role through the device framework
- **AND** writes dispatched through the request layer to the returned block interface MUST preserve the existing whole-sector write validation, read-only rejection, flush/error behavior, and clean-sync assumptions

#### Scenario: 未发布块设备不被文件系统使用
- **WHEN** no probed and published block device exists for the requested filesystem or request-layer role
- **THEN** BigOS MUST return a deterministic initialization or lookup error to the caller
- **AND** the filesystem and request layer MUST NOT directly construct an unrelated hardware backend to bypass the framework

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

