## Purpose

定义 BigOS 内核内部 modern-only virtio-net 网络设备驱动能力，覆盖 PCI 探测与 feature 协商、RX/TX 有界 split virtqueue、MSI-X 完成中断、`bigos::device` 网络设备发布、virtio common helper 复用和默认关闭验证边界。该能力只承诺 frame-level 内核内部网络设备接口，不引入完整网络栈、socket/fd/syscall ABI、用户态网络配置、多队列、packed virtqueue、legacy/transitional virtio 或跨 ISA 后端。

## Requirements

### Requirement: virtio-net modern PCI 探测与协商
BigOS SHALL probe a virtio-net PCI device through the existing PCI/MMIO device path, accept only modern virtio transport with `VIRTIO_F_VERSION_1`, negotiate a bounded required feature set, read the device MAC/MTU state when available, and publish the network device only after device status reaches DRIVER_OK. Probe and feature negotiation MUST run in ordinary blockable kernel context and MUST NOT run from IRQ context.

#### Scenario: 成功发布 modern virtio-net 设备
- **WHEN** a supported modern virtio-net PCI device is present, required MMIO capabilities are mapped, required features are accepted, and queue initialization succeeds
- **THEN** BigOS MUST transition the device through the required virtio status states and publish a ready kernel-internal network device interface
- **AND** the published interface MUST expose deterministic ready/link/MAC/MTU state to kernel-internal consumers

#### Scenario: 拒绝 legacy 或缺失必需 feature
- **WHEN** the device lacks modern virtio PCI capabilities, does not accept `VIRTIO_F_VERSION_1`, or cannot provide the required bounded feature set
- **THEN** BigOS MUST mark probe as unsupported or failed with deterministic diagnostics
- **AND** it MUST NOT publish a ready network device or fall back to legacy IO-port transport

#### Scenario: probe 不在 IRQ 上下文执行
- **WHEN** virtio-net probe, feature negotiation, BAR mapping, or queue allocation is requested from IRQ context
- **THEN** BigOS MUST reject the request or enter a recorded diagnostic path
- **AND** it MUST NOT allocate memory, map MMIO, or program device capabilities from that IRQ context

### Requirement: 有界 RX/TX split virtqueue 初始化
BigOS SHALL initialize one RX split virtqueue and one TX split virtqueue with bounded static capacity, aligned descriptor/available/used memory, and preallocated packet buffers. Queue memory and packet buffers MUST use explicit kernel allocation paths and physical addresses supplied to the virtio common configuration. Queue initialization failure MUST leave the device unpublished.

#### Scenario: 初始化 RX 与 TX 队列
- **WHEN** virtio-net setup reaches queue configuration
- **THEN** BigOS MUST allocate and align descriptor tables, available rings, used rings, and packet buffers for one RX queue and one TX queue
- **AND** queue size and packet buffer count MUST be bounded and MUST NOT depend on unbounded runtime growth

#### Scenario: RX 缓冲预投递
- **WHEN** the RX queue becomes ready
- **THEN** BigOS MUST post a bounded set of empty receive buffers to the available ring before accepting RX interrupts
- **AND** each posted buffer MUST have exactly one tracked ownership state so it cannot be consumed twice

#### Scenario: 队列初始化失败不发布设备
- **WHEN** any queue memory allocation, alignment, physical address resolution, or queue programming step fails
- **THEN** BigOS MUST mark the virtio-net device failed or not-ready with deterministic diagnostics
- **AND** it MUST NOT publish a ready network device interface

### Requirement: TX frame 提交与完成回收
BigOS SHALL provide a kernel-internal bounded TX submission path that copies or references one Ethernet frame into a tracked TX descriptor chain, notifies the device, and reports terminal success only after the TX used ring completion is observed. TX submission MUST distinguish invalid frame size, no free descriptor, device not ready, issue failure, timeout, and device error.

#### Scenario: 提交有效 TX frame
- **WHEN** a kernel-internal consumer submits a valid bounded Ethernet frame to a ready virtio-net device
- **THEN** BigOS MUST construct a TX descriptor chain, write it to the TX available ring, notify the device, and mark the frame pending
- **AND** it MUST NOT report terminal success before the TX used ring reports completion for that descriptor chain

#### Scenario: 拒绝无效或资源不足的 TX
- **WHEN** TX submission receives an invalid frame length, no available descriptor slot, or a not-ready device
- **THEN** BigOS MUST return a deterministic failure status
- **AND** it MUST NOT notify the device with a malformed or untracked descriptor chain

#### Scenario: TX timeout 不被迟到完成覆盖
- **WHEN** a pending TX frame reaches its bounded wait timeout before used completion is observed
- **THEN** BigOS MUST return a deterministic timeout status
- **AND** any later used-ring completion for the stale generation MUST be rejected or recorded without converting the timed-out frame to success

### Requirement: RX frame 接收与归还
BigOS SHALL make received Ethernet frames observable through a kernel-internal bounded RX path after the RX used ring reports device-written buffers. RX completion MUST record frame length and buffer identity, reject malformed lengths, and require the consumer or validation path to return buffers before reposting them to the device.

#### Scenario: 接收有效 RX frame
- **WHEN** the device writes a valid frame into a posted RX buffer and reports it through the RX used ring
- **THEN** BigOS MUST record that frame as available to kernel-internal consumers or validation
- **AND** it MUST preserve the buffer ownership state until the frame is consumed and explicitly returned for reposting

#### Scenario: 拒绝 malformed RX length
- **WHEN** the RX used ring reports a length that is smaller than the required virtio-net header boundary or larger than the tracked packet buffer
- **THEN** BigOS MUST record a deterministic RX error and keep queue ownership consistent
- **AND** it MUST NOT expose the malformed frame as a valid received packet

#### Scenario: RX 缓冲归还后重新投递
- **WHEN** a kernel-internal consumer finishes processing a valid received frame and returns its buffer
- **THEN** BigOS MUST repost that buffer to the RX available ring if the device remains ready
- **AND** it MUST NOT repost a buffer that is still owned by a consumer or already posted to the device

### Requirement: MSI-X 完成中断保持 IRQ-safe
BigOS SHALL connect virtio-net RX/TX completion interrupts through MSI-X vectors that reuse the existing interrupt dispatch ABI and LAPIC EOI boundary. The completion handler MUST parse bounded used-ring entries, update tracked queue state, and wake or mark kernel-internal waiters without allocating memory, blocking, accessing VFS, parsing network protocols, entering syscall/socket paths, or sending i8259 PIC EOI.

#### Scenario: TX/RX used completion 由 MSI-X handler 处理
- **WHEN** virtio-net raises an MSI-X completion interrupt for the RX or TX queue
- **THEN** BigOS MUST dispatch the registered handler, consume bounded used-ring entries, and update the corresponding RX/TX slot states
- **AND** the interrupt path MUST complete through the LAPIC EOI boundary exactly once

#### Scenario: IRQ handler 不执行协议或用户态语义
- **WHEN** the virtio-net MSI-X handler runs
- **THEN** it MUST NOT allocate or free memory, block, submit new unbounded work, access filesystem state, parse IP/TCP/UDP/ARP semantics, or interact with user fd/syscall/socket state
- **AND** it MUST NOT send an i8259 PIC EOI for the MSI-X vector

#### Scenario: 不匹配或重复完成被拒绝
- **WHEN** a used-ring entry references an unknown slot, a stale generation, or a slot already in a terminal state
- **THEN** BigOS MUST reject or diagnose that completion while preserving the current slot owner and terminal result
- **AND** it MUST NOT complete a reused slot or wake the wrong waiter

### Requirement: 网络设备能力保持内核内部
BigOS SHALL expose virtio-net only as a kernel-internal network device interface published through the common `bigos::device` registry under a network device class and explicit network role. The capability MUST NOT create user-visible device nodes, socket syscalls, fd types, mount names, network configuration commands, or protocol-stack claims.

#### Scenario: 通过 bigos::device 选择已发布设备
- **WHEN** virtio-net probe succeeds and a kernel-internal validation or future protocol path requests the ready network device
- **THEN** BigOS MUST return the published interface or deterministic ready status through the common device registry
- **AND** the returned interface MUST provide only bounded frame-level TX/RX and device-state operations

#### Scenario: 未发布设备不可伪造
- **WHEN** no virtio-net device is present, probe failed, negotiation failed, queue setup failed, or the device is not ready
- **THEN** BigOS MUST return deterministic not-found, unsupported, failed, or not-ready state
- **AND** it MUST NOT construct an unrelated fallback network device under the network device role

### Requirement: virtio common helper 保持 transport 与设备语义分离
BigOS SHALL factor shared virtio transport mechanics into a bounded helper layer for modern PCI capability discovery, common configuration access, status transition, feature negotiation, split queue configuration, and queue notification. The helper MUST NOT own virtio-net protocol semantics, virtio-blk request semantics, block cache/writeback behavior, or user-visible device ABI.

#### Scenario: virtio-net 与 virtio-blk 复用 transport helper
- **WHEN** virtio-net or virtio-blk initializes a modern virtio device
- **THEN** BigOS MUST allow the driver to use common helpers for status, feature, capability, queue setup, and notify operations
- **AND** each driver MUST retain its own device-specific request, RX/TX, completion, and validation semantics

#### Scenario: helper 不改变 virtio-blk 行为
- **WHEN** virtio common helper is introduced and virtio-blk remains compiled
- **THEN** existing virtio-blk publication, request-layer integration, completion handling, and validation behavior MUST remain equivalent
- **AND** helper extraction MUST NOT make virtio-blk depend on virtio-net or network validation

#### Scenario: 用户态 ABI 不变
- **WHEN** virtio-net support is compiled in or validation is enabled
- **THEN** existing syscall numbers, fd behavior, process behavior, VFS mounts, and userland baseline semantics MUST remain unchanged
- **AND** user programs MUST NOT gain a socket or network device ABI from this change alone

### Requirement: virtio-net 验证可复现且不改变默认启动
BigOS SHALL provide default-off QEMU tap-backed validation for virtio-net that covers device publication, queue setup, TX completion, controlled RX receipt, deterministic error or timeout handling, and default boot regression when the required toolchain and emulator support are available. BigOS SHALL include a host-side minimal tap setup/cleanup helper for the validation path. Validation MUST record skipped coverage and residual risk when QEMU, tap setup, host permissions, MSI-X, virtio-net backend, serial capture, or cross-toolchain prerequisites are unavailable.

#### Scenario: 宿主侧 tap 配置脚本可复现
- **WHEN** the host platform supports the required tap backend and the validation helper is invoked with sufficient permissions
- **THEN** the helper MUST create or prepare the minimal tap configuration needed by the QEMU virtio-net validation path and document the packet injection prerequisites
- **AND** it MUST provide a cleanup path or equivalent instructions that avoid leaving unexplained host networking state after validation

#### Scenario: smoke 覆盖 TX/RX 闭环
- **WHEN** the expected cross toolchain, kernel build configuration, and a QEMU environment with modern virtio-net, MSI-X, and tap backend support are available and the default-off validation is enabled
- **THEN** validation MUST publish the device, initialize RX/TX queues, complete at least one TX frame, and observe at least one controlled RX frame through the driver queue state
- **AND** success MUST depend on the driver observing terminal completion rather than only on private setup state

#### Scenario: smoke 覆盖失败路径
- **WHEN** validation exercises a missing device, unsupported feature set, queue setup failure, malformed RX length, no TX descriptor, or TX timeout path
- **THEN** BigOS MUST record a deterministic failure category for that condition
- **AND** it MUST NOT collapse distinct probe, queue, TX, RX, timeout, and environment-skip results into a generic success or generic driver failure

#### Scenario: 默认启动路径不依赖 virtio-net
- **WHEN** virtio-net validation is disabled or no virtio-net device is attached
- **THEN** BigOS MUST keep the existing default boot, storage, filesystem, `/rw`, shell, and userland baseline independent of virtio-net availability
- **AND** absence of virtio-net MUST NOT prevent normal boot validation from running

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU, tap setup, host permissions, Bochs default-boot validation, cross binutils, ROM/display dependencies, serial capture, modern virtio-net, MSI-X delivery, or backend packet injection is unavailable
- **THEN** validation records MUST identify the skipped coverage and remaining risk
- **AND** they MUST NOT claim runtime virtio-net smoke success for a skipped environment
