## Context

BigOS 当前已经具备设备与驱动注册/probe 框架、设备框架发布的 ATA PIO 块设备、块 I/O 请求层、page/buffer cache，以及 bounded `/rw` 文件系统路径。现有设计已经把普通存储消费者逐步收敛到发布后的 `BlockDevice` 与请求层之上，但用于正常路径的硬件后端仍主要是 ATA PIO。

本设计新增一个 RAM 块设备后端，作为第二个块设备后端验证设备框架与请求层边界。该后端不代表新的用户可见存储设备，也不替代当前 RAM-backed `/rw` 文件系统语义；它是内核内部、固定容量、整扇区读写的块后端，用于证明同一块设备类别可以承载非 ATA 实现，并让请求层/cache 在第二后端上执行确定性读写验证。

该变更不改变 x86_64 Legacy BIOS boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、MBR/exFAT 发现、persistent `/rw` clean-sync 语义或用户态 ABI。probe、读写和验证路径只在普通可阻塞内核上下文运行。

## Goals / Non-Goals

**Goals:**

- 提供 freestanding-safe、固定容量、整扇区读写的 RAM 块设备后端。
- 将 RAM 块后端接入设备注册/probe 框架，并通过稳定内部角色发布为第二个块设备后端。
- 让块 I/O 请求层能够对第二个已发布后端执行同步读写，并验证按设备队列隔离、状态传播和 cache 对接。
- 保持 ATA PIO、boot disk、persistent `/rw` 和默认启动路径行为不变。
- 提供默认关闭验证，用于覆盖 RAM 后端读写、越界/缓冲校验、未发布设备拒绝、队列隔离和 cache 往返。

**Non-Goals:**

- 不实现 virtio、AHCI/SATA、NVMe、USB storage、PCI/ACPI 枚举、DMA 或新 ISA。
- 不实现 async I/O、后台 worker、completion queue、多队列调度或 SMP I/O 并发。
- 不暴露用户可见设备节点、syscall ABI、分区表、挂载接口或完整 POSIX 块设备模型。
- 不改变 UEFI spike 的 runtime parity 状态。
- 不把 RAM 块后端作为 crash recovery、journaling 或完整持久化能力。

## Decisions

1. RAM 块后端采用固定容量整扇区模型。
   - 原因：现有块设备契约以整扇区读写为边界，固定容量更容易做越界、缓冲区长度和清零初始化验证，也避免引入动态扩容和复杂生命周期。
   - 备选：实现可增长内存盘。该方案更接近通用 ramdisk，但会扩大到分配失败、碎片、大小调整和挂载生命周期，不适合作为框架验证后端。

2. RAM 块后端通过设备框架注册、probe、发布，而不是由验证代码直接构造后交给请求层。
   - 原因：目标是验证设备与驱动框架能够承载第二块设备后端。直接构造会绕过 registry/probe/publication 关键路径，无法证明普通消费者与具体 ATA 初始化解耦。
   - 备选：只在 block I/O smoke 中使用临时 fake device。现有请求层已经有 fake device 验证，这不足以验证 framework integration。

3. 后端使用新的内核内部稳定角色，而不是复用 boot disk 或 persistent writable disk 角色。
   - 原因：复用现有角色容易改变默认挂载和持久路径语义。独立角色可以让验证路径明确选择 RAM 后端，同时保证 boot disk 与 persistent `/rw` 的默认行为不变。
   - 备选：把 persistent writable block 默认切到 RAM 块后端。该方案会混淆现有 RAM-backed `/rw` 与持久 clean-sync 边界，并可能削弱 ATA 写路径覆盖。

4. RAM 后端读写走现有 `BlockDevice` 与请求层状态语义。
   - 原因：第二后端的价值在于复用相同上层契约。越界、缓冲区过小、只读/unsupported、设备未就绪和后端错误应继续用确定性状态表达。
   - 备选：为 RAM 后端添加专用读写 API。该方案会引入并行契约，削弱对请求层和 cache 泛化能力的验证。

5. 验证默认关闭，并复用现有 smoke 风格。
   - 原因：RAM 后端主要用于框架验证，不应改变普通启动行为或增加默认启动耗时。默认关闭 smoke 可以在 QEMU/Bochs 可用时做可重复覆盖。
   - 备选：每次普通启动都创建并验证 RAM 后端。该方案会让验证逻辑进入正常路径，并增加初始化顺序噪声。

## Risks / Trade-offs

- [Risk] 新增 RAM 后端可能与现有 RAM-backed `/rw` 文件系统概念混淆。→ Mitigation：文档和命名明确它是块设备后端验证能力，不代表用户可见 `/rw` 语义变更。
- [Risk] 增加新的设备角色可能扩大设备框架枚举和容量边界。→ Mitigation：角色仍为内核内部稳定标识，registry 容量保持有界，重复注册和容量耗尽继续确定性失败。
- [Risk] 如果验证路径绕过请求层，无法证明第二后端对 cache 有用。→ Mitigation：任务和规格要求至少覆盖通过请求层的同步读写，并包含 cache 往返验证。
- [Risk] 固定容量 RAM 后端不覆盖真实硬件超时和端口 I/O。→ Mitigation：该后端只验证框架泛化能力；ATA PIO 仍负责硬件轮询、错误和真实磁盘路径覆盖。
- [Risk] 静态容量过大可能增加内核镜像或早期内存压力。→ Mitigation：容量必须有界且面向验证，若使用动态分配则只在内存管理初始化后的普通上下文执行，并记录失败状态。

## Migration Plan

1. 新增 RAM 块设备后端接口和实现，定义固定扇区大小、容量、读写、清零初始化和失败状态。
2. 扩展设备框架的内部角色/driver id/注册表容量检查，使 RAM 块后端可以注册、probe 并发布为独立块设备。
3. 将请求层验证扩展到第二后端，覆盖同步读写、队列隔离、未发布/未 ready 拒绝和错误传播。
4. 增加 cache 往返验证，确认第二后端经请求层装入、修改、写回、再次读取时保持内容一致。
5. 保持普通 boot disk 和 persistent `/rw` 初始化顺序不变；如验证失败，回滚策略是移除 RAM 后端注册和 smoke 调用，不改变 ATA PIO、磁盘格式或用户态 ABI。

## Open Questions

- 暂无。RAM 块后端作为内核内部验证后端，不作为用户可见设备或默认持久存储后端。
