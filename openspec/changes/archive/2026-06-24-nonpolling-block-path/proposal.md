## Why

现有块请求层已经有 pending/completion 和 scheduler wakeup 基础，但默认块路径仍在提交调用栈中执行同步轮询，导致 cache、writeback 和文件系统 I/O 仍被具体轮询后端绑定。本变更需要把既有块路径迁到有界的非轮询完成边界上，使同步调用者继续获得最终状态，同时为后续现代存储驱动和更完整异步生命周期提供可复用的内核内部接口。

## What Changes

- 将既有块请求提交从“直接调用同步轮询设备方法并在调用栈内完成”调整为“请求层 issue，设备或验证后端通过 completion 边界完成，提交线程按现有同步 API 等待最终状态”。
- 扩展块设备接口边界，使当前后端可以声明同步兼容路径或中断完成路径；非轮询路径必须通过有界 request/completion 身份返回状态。
- 改造 ATA PIO 当前路径的默认块 I/O 使用方式，移除请求层消费者对同步轮询完成的依赖；必要的设备状态等待必须保持有界，并以 request-layer 状态传播。
- 保持 page/buffer cache、dirty writeback、`fsync`/`sync`、persistent `/rw` clean-sync 和读写错误传播语义不变。
- 补充默认关闭验证，覆盖 cache read miss、dirty writeback、persistent `/rw` clean-sync、timeout/error 和 completion 与同步 wrapper 的闭环。
- 保持 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、用户态 ABI、现有文件系统格式和默认启动设备选择不变。
- 明确非目标：不实现 virtio、AHCI/SATA、NVMe、DMA、多队列调度、完整 I/O scheduler、后台 writeback、用户态 async I/O syscall、poll/select/epoll、网络栈或 UEFI runtime parity。

## Capabilities

### New Capabilities

- `nonpolling-block-path`: 定义既有块路径迁出同步轮询后的内核内部提交、完成、等待、错误传播和验证边界。

### Modified Capabilities

- `block-io-request-layer`: 将同步提交契约收紧为可阻塞 wrapper，要求可中断完成的设备路径通过 request/completion 身份完成，且不得绕过请求层恢复同步轮询完成依赖。
- `interrupt-driven-io-completion`: 将已有 completion 模型从验证/fake producer 扩展到真实块路径可用的 issue-to-complete 闭环，约束设备 IRQ 或等价完成源的状态交接。
- `page-buffer-cache`: 明确 cache load/writeback 在块路径迁出轮询后仍保持现有 dirty、eviction、writeback failure 和 process-context-only 语义。

## Impact

- 影响子系统：块 I/O 请求层、块设备接口、ATA PIO 后端、scheduler wait queue/wakeup、IRQ/completion 交接、page/buffer cache、persistent `/rw` clean-sync 和默认关闭块 I/O smoke。
- 架构假设：目标仍为 x86_64 Legacy BIOS/MBR/exFAT 默认运行路径；SMP/APIC 基线可存在但本变更不引入新的 ISA、非 x86 后端或 UEFI runtime parity。
- 内存与布局假设：请求、completion token、等待队列和设备私有状态必须有界；IRQ/completion 路径不得分配、释放、阻塞、访问用户内存、执行文件系统/cache writeback 或依赖 hosted runtime。
- 模拟器与磁盘假设：QEMU/Bochs 的 Legacy IDE/ATA 路径仍是主要验证对象，现有 boot disk、persistent test disk 和文件系统布局不改变。
- 工具链假设：实现和验证继续使用 xmake、x86_64-elf 工具链、freestanding C++17，以及可用的 QEMU/Bochs；若 emulator、ROM/display、磁盘镜像或 cross-binutils 不可用，验证记录必须显式说明跳过项和残余风险。
