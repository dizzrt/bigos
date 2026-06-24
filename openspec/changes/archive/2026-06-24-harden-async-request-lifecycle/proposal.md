## Why

现有块 I/O 已经迁入非轮询完成边界，但请求从 issue 到 terminal completion 的生命周期、超时后的归属、取消/迟到完成处理和诊断语义仍需要进一步收紧。现在需要把异步请求状态机和诊断约束固化为有界契约，避免后续现代存储驱动、writeback 扩展或更多并发路径建立在含糊的完成语义上。

## What Changes

- 引入有界异步请求生命周期能力，明确 request slot、generation、completion token、pending wait、timeout/cancel、terminal state 和 slot reuse 的状态转换。
- 收紧块请求层行为：每个请求必须 exactly-once 到达 terminal 状态，迟到、重复、跨设备或 generation 不匹配的 completion 必须被拒绝并可诊断。
- 收紧 IRQ/completion 边界：IRQ-safe 完成路径只能执行有界状态发布和 wakeup，不得分配、阻塞、访问用户内存、执行 cache/filesystem policy 或拥有 EOI。
- 为非轮询块路径补充生命周期诊断：issue failure、queue full、timeout、device error、completion rejection、slot reuse、dirty writeback failure 等必须有确定性状态和诊断边界。
- 增加默认关闭验证，覆盖成功完成、错误完成、超时、取消或等价失效路径、重复/迟到 completion 拒绝、诊断稳定性和默认启动回归。
- 保持 boot handoff、链接地址、页表布局、IDT/syscall vector、磁盘布局、用户态 ABI、文件系统格式、cache/writeback 用户可见语义和默认启动设备选择不变。
- 明确非目标：不实现用户态 async I/O syscall、poll/select/epoll、后台 writeback、I/O scheduler、多队列调度、DMA、virtio、AHCI/SATA、NVMe、网络栈、完整 APIC IRQ 重构或 UEFI runtime parity。

## Capabilities

### New Capabilities

- `async-request-lifecycle`: 定义内核块 I/O 异步请求从分配、issue、pending、completion、timeout/cancel 到回收复用的有界生命周期和诊断契约。

### Modified Capabilities

- `block-io-request-layer`: 收紧请求层的 terminal state、slot reuse、timeout/cancel、completion rejection 和同步 wrapper 状态传播需求。
- `interrupt-driven-io-completion`: 收紧 IRQ-safe completion handoff 的上下文边界、exactly-once 发布、迟到/重复完成拒绝和 wakeup 语义。
- `nonpolling-block-path`: 明确非轮询块路径必须通过统一生命周期诊断暴露 issue、device、timeout 和 completion failure，且不改变 cache/writeback 成功边界。
- `runtime-smoke-validation`: 增加默认关闭验证对异步请求生命周期、诊断稳定性和默认启动回归的覆盖要求。

## Impact

- 影响子系统：块 I/O 请求层、completion token、scheduler wait/wakeup、ATA PIO 非轮询路径、RAM/smoke producer、page/buffer cache、persistent `/rw` clean-sync、运行时 smoke 和串口/VGA 诊断。
- 架构假设：目标仍为 x86_64 freestanding kernel；默认可运行基线仍是 Legacy BIOS/MBR/exFAT，SMP/APIC/UEFI 可作为并行已有能力存在但不在本变更中扩展。
- 内存与布局假设：请求队列、token、诊断记录和验证状态必须有界；IRQ/completion 路径不得依赖动态分配、异常、RTTI、hosted libc、用户地址空间或大型栈对象。
- 模拟器与磁盘假设：QEMU/Bochs Legacy IDE/ATA 路径仍是主要运行验证对象；boot disk、persistent test disk、exFAT 和 `/rw` 布局不改变。
- 工具链假设：实现和验证继续使用 xmake、x86_64-elf 工具链、freestanding C++17，以及可用的 QEMU/Bochs；若本地 emulator、ROM/display、磁盘镜像或 cross-binutils 不可用，验证记录必须显式说明跳过项和残余风险。
