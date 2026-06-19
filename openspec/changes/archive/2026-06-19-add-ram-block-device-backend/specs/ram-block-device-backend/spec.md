## ADDED Requirements

### Requirement: 有界 RAM 块设备后端
BigOS SHALL provide a freestanding-safe RAM-backed block device backend with fixed capacity, fixed sector size, whole-sector read/write operations, deterministic initialization, and deterministic status reporting. The backend MUST NOT require hosted runtime services, exceptions, RTTI, dynamic linking, unbounded allocation, or uncontrolled global constructors.

#### Scenario: 初始化后端成功
- **WHEN** kernel initialization or a default-off validation path initializes the RAM block backend with a supported fixed sector size and bounded capacity
- **THEN** BigOS MUST prepare the backing storage for whole-sector block operations
- **AND** subsequent valid reads from unwritten sectors MUST return deterministic zeroed or explicitly initialized contents

#### Scenario: 容量或扇区配置非法
- **WHEN** the RAM block backend is initialized with zero sectors, an unsupported sector size, or capacity arithmetic that would overflow
- **THEN** BigOS MUST reject initialization with a deterministic error
- **AND** it MUST NOT publish a usable block interface for that invalid backend

### Requirement: RAM 块读写契约
The RAM block backend SHALL implement the existing kernel `BlockDevice` whole-sector read and write contract. It MUST validate sector count, buffer length, and LBA range before copying data, and it MUST preserve deterministic status values for invalid requests without partially copying accepted data outside the valid range.

#### Scenario: 整扇区写入后读取一致
- **WHEN** the block I/O request layer writes one or more valid whole sectors to the RAM block backend and then reads the same sector range
- **THEN** the read MUST return the bytes from the latest successful write
- **AND** the backend MUST return success for both completed operations

#### Scenario: 读写越界被拒绝
- **WHEN** a read or write request targets a sector range outside the fixed RAM block capacity or overflows range arithmetic
- **THEN** the backend MUST reject the request before copying data
- **AND** it MUST return a deterministic invalid-range or device error status

#### Scenario: 缓冲区过小被拒绝
- **WHEN** a read destination or write source buffer is smaller than the requested sector byte count
- **THEN** the backend MUST reject the request before copying data
- **AND** it MUST leave the backend contents unchanged for rejected writes

### Requirement: RAM 后端通过设备框架发布
BigOS SHALL register, probe, and publish the RAM block backend through the kernel device-driver framework before the request layer or validation consumers use it. The backend MUST use a kernel-internal stable role that does not replace the boot disk or persistent writable disk role by default.

#### Scenario: probe 成功后可查找
- **WHEN** the device framework probes a valid RAM block backend descriptor in ordinary blockable kernel context
- **THEN** BigOS MUST publish a class-specific `BlockDevice` interface for the RAM block role
- **AND** framework lookup for that role MUST return the published RAM block interface

#### Scenario: 未发布后端不可消费
- **WHEN** the RAM block backend has not been registered, probe has not run, or probe failed
- **THEN** request-layer or validation consumers MUST observe a deterministic not-ready or not-found status
- **AND** they MUST NOT fabricate a usable RAM block device outside the framework

#### Scenario: 不替换默认存储角色
- **WHEN** the RAM block backend is registered for validation
- **THEN** BigOS MUST keep the boot disk and persistent writable disk roles bound to their existing configured backends unless an explicit validation path selects the RAM role
- **AND** it MUST NOT change normal MBR/exFAT discovery, persistent clean-sync `/rw`, or user-visible file behavior

### Requirement: RAM 后端验证可复现
BigOS SHALL provide deterministic default-off validation for the RAM block backend. Validation MUST cover framework publication, request-layer read/write submission, invalid range or buffer rejection, and page/buffer cache round-trip behavior where practical.

#### Scenario: smoke 覆盖第二后端读写
- **WHEN** the RAM block backend validation path is enabled in an environment with the expected toolchain and emulator support
- **THEN** validation MUST probe the RAM backend through the device framework
- **AND** it MUST execute at least one successful write/read round trip through the block I/O request layer

#### Scenario: smoke 覆盖 cache 往返
- **WHEN** validation uses the page/buffer cache with the published RAM block backend
- **THEN** BigOS MUST write data through the cache path, sync or otherwise force the backend-visible update according to the cache contract, and read the same data back deterministically
- **AND** validation MUST fail deterministically if the cache bypasses the request layer or loses dirty data

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU, Bochs, cross-binutils, ROM/display dependencies, or disk image setup required by runtime validation are unavailable
- **THEN** validation notes MUST record the skipped coverage and residual risk
- **AND** they MUST NOT claim runtime smoke success for the skipped environment

### Requirement: RAM 后端边界不扩大系统承诺
The RAM block backend SHALL remain a bounded kernel-internal validation backend. Documentation and diagnostics MUST NOT describe it as a complete ramdisk subsystem, user-visible device node, persistent storage replacement, broad storage support, async I/O facility, SMP I/O capability, or UEFI runtime-parity backend.

#### Scenario: 文档保持有界描述
- **WHEN** implementation notes, validation records, or project documentation describe the RAM block backend
- **THEN** they MUST identify it as a bounded second block backend for framework and request-layer validation
- **AND** they MUST NOT claim new hardware storage support, complete device modeling, or user-visible block device semantics
