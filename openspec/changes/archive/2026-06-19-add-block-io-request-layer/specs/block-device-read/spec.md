## MODIFIED Requirements

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
