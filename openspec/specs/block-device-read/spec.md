## Purpose

Define the first kernel-only block device read capability for BigOS: a bounded
whole-sector read API, a synchronous ATA PIO read-only backend for the current
Bochs raw disk setup, explicit non-IRQ context limits, and deterministic smoke
diagnostics. This capability is intentionally read-only and does not introduce
async IO, request queues, write support, caching, partition management, or
filesystem semantics.

## Requirements

### Requirement: Kernel block device read API

BigOS SHALL provide a kernel-only block device read interface that reads whole sectors from an LBA address into a caller-provided kernel buffer. The API MUST validate sector count, destination buffer length, and arithmetic overflow before issuing device I/O.

#### Scenario: Aligned sector read succeeds

- **WHEN** kernel code requests one or more sectors from a registered read-only block device with a destination buffer large enough for `sector_count * sector_size`
- **THEN** the block device API reads the requested sectors into the destination buffer and returns success.

#### Scenario: Buffer is too small

- **WHEN** kernel code requests sectors with a destination buffer smaller than the required byte count
- **THEN** the block device API rejects the request before issuing device I/O and returns a bounded error status.

#### Scenario: LBA arithmetic overflows

- **WHEN** kernel code requests a sector range whose end LBA calculation overflows the address type
- **THEN** the block device API rejects the request before issuing device I/O and returns a bounded error status.

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

BigOS SHALL 在现有只读块读接口之上新增一个内核块设备写接口，把整扇区从调用方缓冲写入某 LBA 地址。`BlockDevice` MUST 以追加写入口（如 `write_impl`）暴露写能力，`write_impl` 为空的设备 MUST 被视为只读；写 API MUST 在发起设备写前校验扇区数、源缓冲长度与算术溢出，且 MUST NOT 改变现有只读读路径、扇区大小或 MBR/exFAT 发现契约。

#### Scenario: Aligned sector write succeeds

- **WHEN** 内核对一个具备写后端、源缓冲足够容纳 `sector_count * sector_size` 的可写块设备请求写一个或多个扇区
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

### Requirement: Block write context boundary

块设备写 SHALL 仅在端口 I/O 与内存管理初始化后的可阻塞普通内核上下文调用。写 API MUST NOT 被标注为 IRQ-handler-safe、preemption-safe、异步或可在不可阻塞上下文调用。

#### Scenario: Non-IRQ context write

- **WHEN** 内核在可阻塞普通上下文（如经块缓冲缓存落盘）调用块写 API
- **THEN** 该调用在支持的上下文边界内，MAY 执行同步轮询写 I/O

#### Scenario: IRQ context write is out of scope

- **WHEN** 代码试图把块写 API 当作 IRQ-handler-safe 行为使用
- **THEN** 规范不保证正确性，实现文档 MUST 标注该用法不支持
