## Purpose

Define the bounded kernel-internal device and driver registration/probe framework
for BigOS. The framework gives existing hardware backends a deterministic
registry, explicit probe/publication state, class-specific interface lookup, and
context boundaries without introducing hotplug, PCI/ACPI enumeration, a complete
bus model, async I/O, SMP, or user-visible device nodes.

## Requirements

### Requirement: 有界设备与驱动 registry
BigOS SHALL provide a freestanding-safe kernel device and driver registry with bounded static capacity. The registry MUST record device class, stable instance or role, driver binding state, class-specific interface pointer, private context, and deterministic status without relying on hosted runtime services, exceptions, RTTI, dynamic linking, or uncontrolled global constructors.

#### Scenario: 注册设备和驱动成功
- **WHEN** kernel initialization registers a valid device descriptor and a compatible driver descriptor before consumers query devices
- **THEN** BigOS MUST store both descriptors in the bounded registry
- **AND** later registry queries MUST be able to observe their registered state

#### Scenario: 重复注册被拒绝
- **WHEN** kernel code registers another device or driver with the same class and stable identity as an existing registry entry
- **THEN** BigOS MUST reject the duplicate registration with a deterministic error
- **AND** it MUST NOT overwrite the existing entry or publish a partially replaced device

#### Scenario: registry 容量耗尽
- **WHEN** the bounded device or driver table has no free slot for a new registration
- **THEN** BigOS MUST return a deterministic no-space error
- **AND** it MUST leave all previously registered entries intact

### Requirement: 显式 probe 与发布状态
BigOS SHALL separate device registration from driver probe and publication. A registered device MUST NOT be returned to consumers as usable until a compatible driver probe has succeeded and published the class-specific interface.

#### Scenario: probe 成功后发布设备
- **WHEN** the framework probes a registered device with a compatible driver and the driver reports success
- **THEN** BigOS MUST mark the device as probed or published
- **AND** class-specific lookup MUST return the published interface for that device

#### Scenario: probe 失败不发布设备
- **WHEN** a compatible driver returns a deterministic failure during probe
- **THEN** BigOS MUST preserve the device's failure status
- **AND** consumer lookup MUST NOT return the failed device as usable

#### Scenario: 未 probe 设备不可消费
- **WHEN** kernel code queries a device that has been registered but not successfully probed
- **THEN** BigOS MUST return a deterministic not-ready or not-found status
- **AND** it MUST NOT expose an uninitialized class-specific interface pointer

### Requirement: 按类别和稳定角色查找设备
BigOS SHALL allow kernel subsystems to resolve published devices by device class and a stable role or instance identifier. Lookup MUST be deterministic and MUST NOT require consumers to know concrete hardware initialization details.

#### Scenario: 查找已发布块设备
- **WHEN** a filesystem subsystem requests a published block device for a stable role such as the boot disk or persistent writable disk
- **THEN** BigOS MUST return the corresponding class-specific block interface
- **AND** the filesystem MUST NOT need to directly initialize the concrete ATA PIO backend

#### Scenario: 内部角色不暴露为用户 ABI
- **WHEN** BigOS distinguishes the boot disk from the persistent writable disk in the device framework
- **THEN** it MUST use kernel-internal stable roles or identifiers for that distinction
- **AND** it MUST NOT expose those identifiers as a new user-visible syscall ABI, filesystem-visible device numbering contract, or external validation-tool ABI

#### Scenario: 查找不存在的设备
- **WHEN** a subsystem requests a device class or role that has no published entry
- **THEN** BigOS MUST return a deterministic not-found or not-ready status
- **AND** it MUST NOT fabricate a device or fall through to unrelated hardware state

### Requirement: probe 上下文边界
BigOS SHALL run device registration and probe only from supported kernel initialization or ordinary blockable kernel context. Probe paths that may perform port I/O, polling block I/O, allocation, or cache interaction MUST NOT run from IRQ context, scheduler critical sections, preemption-disabled regions, or other nonblocking paths.

#### Scenario: 初始化上下文执行 probe
- **WHEN** the kernel invokes device framework initialization and probe after the required low-level port I/O and memory facilities are available
- **THEN** probe MAY perform bounded synchronous hardware checks according to each driver contract
- **AND** successful devices MAY be published to later consumers

#### Scenario: 不可阻塞上下文拒绝 probe
- **WHEN** an IRQ handler, timer tick, scheduler critical section, preemption-disabled region, or equivalent nonblocking path attempts to register or probe a device that may block or poll hardware
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT issue blocking I/O or publish a device from that context

### Requirement: timer/video/rtc 接入正常初始化路径
BigOS SHALL register, probe, and publish the existing PIT timer, VGA text device, and CMOS RTC through the device-driver framework as part of their normal kernel initialization path. This integration MUST preserve the existing hardware constants, interrupt vector assignments, PIT channel behavior, VGA text output behavior, CMOS RTC read boundary, and deterministic failure diagnostics.

#### Scenario: PIT timer 经框架初始化
- **WHEN** kernel timer initialization enables the PIT-backed timer path
- **THEN** BigOS MUST use a framework-published timer device or timer class wrapper for the normal PIT initialization path
- **AND** the PIT channel configuration, timer IRQ vector, tick behavior, and nonblocking interrupt handler boundary MUST remain compatible with the existing timer contract

#### Scenario: VGA text device 经框架初始化
- **WHEN** kernel console or early text output initializes the VGA text device after the framework is available
- **THEN** BigOS MUST use a framework-published video device or video class wrapper for the normal VGA path
- **AND** visible text output behavior and existing VGA port/MMIO assumptions MUST remain compatible with the current console contract

#### Scenario: CMOS RTC 经框架初始化
- **WHEN** wall-clock initialization reads the CMOS RTC
- **THEN** BigOS MUST use a framework-published RTC device or RTC class wrapper for the normal CMOS RTC path
- **AND** the one-shot bounded CMOS read semantics MUST remain compatible with the existing wall-clock contract

### Requirement: 现有启动和硬件边界保持稳定
BigOS SHALL introduce the device framework without changing the current x86_64 Legacy BIOS boot ABI, linker addresses, page-table layout, interrupt vectors, syscall ABI, disk layout, or user-visible bounded userland behavior.

#### Scenario: 默认启动路径保持兼容
- **WHEN** the device framework is present in a normal x86_64 Legacy BIOS boot
- **THEN** the kernel MUST keep the existing boot handoff, interrupt/syscall vector assignments, page-table layout, and user-visible process behavior unchanged
- **AND** device registration MUST NOT require UEFI runtime parity, SMP, async I/O, or a new storage backend

#### Scenario: 框架不声明完整设备模型
- **WHEN** documentation or validation describes the new device framework
- **THEN** it MUST identify the framework as a bounded kernel-internal registration/probe boundary
- **AND** it MUST NOT claim hotplug, PCI/ACPI enumeration, power management, complete bus modeling, user-visible device nodes, or broad storage/device support

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
