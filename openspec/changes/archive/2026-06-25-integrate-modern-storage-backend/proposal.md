## Why

现代块存储驱动已经具备独立后端与请求完成能力，但默认块层、缓存和持久写回路径仍需要一个有界的集成契约来选择、使用并验证该后端。现在补齐这层集成，可以在不改变默认启动磁盘布局和用户 ABI 的前提下，让现代存储后端参与真实 cache/writeback 行为验证。

## What Changes

- 将现代块存储后端纳入块设备选择与请求提交路径，使块 I/O 请求层可以对该后端执行有界同步 wrapper、状态传播和上下文检查。
- 让 page/buffer cache 可以以现代存储后端作为目标设备执行读装入、dirty 写回、淘汰写回和设备级同步，并保持现有 dirty/failure 语义。
- 允许持久 clean-sync `/rw` 验证在显式选择的现代存储后端上运行，覆盖同步后读回、写回失败保留状态和跨 clean reboot 可解释性。
- 增加默认关闭验证路径，覆盖现代后端经块层、cache 与 writeback 的读写往返、错误传播、默认启动回归和环境不可用记录。
- 保持默认 Legacy BIOS/ATA/exFAT/userland baseline 不变；现代后端不成为默认启动、挂载或用户 ABI 的必要条件。

## Capabilities

### New Capabilities
- 无。

### Modified Capabilities
- `modern-block-storage-driver`: 增加现代存储后端被块层、缓存和写回路径显式选择并参与集成验证的契约。
- `block-io-request-layer`: 增加对现代块存储后端的有界提交、状态传播、队列隔离和上下文边界要求。
- `page-buffer-cache`: 增加以现代块存储后端为目标设备的 cache load/writeback/sync 语义要求。
- `persistent-writable-filesystem`: 增加持久 clean-sync `/rw` 在显式现代存储后端上运行时的同步与验证边界。

## Impact

- 受影响子系统：设备/驱动框架、块 I/O 请求层、page/buffer cache、持久 clean-sync `/rw`、默认关闭运行时 smoke、仿真器磁盘配置辅助路径。
- 架构假设：仍以 x86_64 Legacy BIOS 当前可运行路径为默认基线；不改变内核链接地址、页表布局、IDT/syscall 向量、CR3 切换或用户态 ABI。
- 内存与上下文假设：cache 装入和写回仍只在可阻塞内核上下文执行；IRQ/completion 路径只完成已 pending 请求，不执行 cache 或 filesystem policy。
- 磁盘布局假设：默认 boot/exFAT/ATA 路径保持兼容；现代存储后端通过显式选择或默认关闭验证使用，不自动替换启动盘或 `/rw` 后端。
- 工具链与仿真器假设：运行时验证依赖 xmake、x86_64-elf toolchain，以及支持现代存储设备配置的 QEMU/Bochs 路径；不可用时必须记录跳过项与残余风险。
- 非目标：不引入新 ISA、NVMe 第二驱动、legacy/transitional virtio、packed ring、多队列调度、用户可见 async I/O、设备节点、动态挂载接口、完整 POSIX 存储栈、崩溃一致性或 power-loss recovery。
