## Why

当前块设备读写、持久 `/rw` 与 page/buffer cache 已经形成可用的同步存储基线，但上层仍直接依赖同步块设备入口，缺少一个明确的请求提交、排队、完成状态与缓存对接边界。
本变更引入有界块 I/O 请求层，使现有同步路径先具备统一调度点和诊断契约，并为后续 async I/O 留出接口空间，而不改变当前 x86_64 Legacy BIOS 默认运行目标。

## What Changes

- 新增 freestanding-safe 的内核块 I/O 请求层，定义有界请求描述、提交、队列、同步完成和确定性错误状态。
- 将现有块设备读写后端保留为底层执行接口，由请求层统一提交整块/整扇区读写请求。
- 让 page/buffer cache 的装入、写回、设备范围同步和 dirty victim 淘汰通过请求层执行块 I/O。
- 保持当前同步、单核、普通可阻塞内核上下文边界；请求队列是有界同步队列，不声明 async I/O、SMP I/O 调度、多队列或 elevator 调度。
- 保持现有 x86_64 Legacy BIOS 启动、磁盘布局、MBR/exFAT 发现、persistent `/rw` clean-sync 语义和用户态 ABI 不变。

## Capabilities

### New Capabilities
- `block-io-request-layer`: 定义块 I/O 请求抽象、有界队列、同步提交/完成、确定性错误传播，以及未来 async I/O 可复用但当前不启用的边界。

### Modified Capabilities
- `block-device-read`: 将现有块设备读写 API 明确为请求层下方的设备执行接口，并约束普通消费者经请求层提交块 I/O。
- `page-buffer-cache`: 将缓存装入、写回、设备范围同步和淘汰写回的块 I/O 对接点调整为请求层，同时保留原有 dirty/error 语义。

## Impact

- 影响子系统：内核块设备层、设备框架发布的块接口、page/buffer cache、persistent `/rw` 写回路径和相关默认关闭 smoke。
- 架构假设：继续以 x86_64 Legacy BIOS/MBR/exFAT/bigfs 路径为默认验证目标，UEFI runtime parity、新 ISA 和第二块设备后端不属于本变更。
- 内存与上下文假设：请求对象和队列容量保持有界；提交、装入和写回仅在允许阻塞、允许同步块 I/O 的普通内核上下文执行。
- 工具链与模拟器假设：继续使用 xmake、x86_64-elf 工具链，以及 QEMU/Bochs 进行可用性范围内的默认关闭 smoke 验证。
- 非目标：不实现 async I/O、DMA、virtio/AHCI/NVMe、新存储驱动、分区管理、用户可见设备节点、完整 I/O 调度器、SMP I/O 并发或 crash-recovery 级持久化。
