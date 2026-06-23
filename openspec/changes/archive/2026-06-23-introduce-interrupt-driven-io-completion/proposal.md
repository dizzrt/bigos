## Why

现有块 I/O 请求层已经具备有界请求描述、按设备队列和确定性同步完成状态，但硬件路径仍把完成等待绑定在提交调用栈里的同步轮询上。本变更需要先建立可审计的中断驱动完成模型，让块请求能够被设备 IRQ 标记完成并唤醒等待线程，为后续把现有块路径迁出同步轮询打基础。

## What Changes

- 新增有界的 I/O completion 状态模型，覆盖 pending、success、确定性错误、完成者身份、一次性完成和重复完成拒绝。
- 扩展块 I/O 请求层，使请求可以进入等待完成状态，并通过 IRQ-safe completion path 记录最终状态。
- 将 completion 与现有 scheduler wait queue wakeup 集成，使普通可阻塞内核线程可以等待请求完成，IRQ handler 只执行有界状态更新和唤醒。
- 保持现有同步提交 API 和 page/buffer cache 语义可用；默认块路径是否从同步轮询迁出留给后续变更处理。
- 顺带将 boot/debug 与 runtime smoke 相关日志输出默认目录从 `log/` 迁移到 `logs/`，并同步 xmake wrapper、Python helper、测试和双语文档示例。
- 保持现有 ATA PIO、RAM block 后端、boot disk、persistent `/rw`、MBR/exFAT/bigfs、用户态 ABI、IDT/syscall vector、页表布局和默认启动路径不变。
- 明确非目标：不实现 virtio、AHCI/SATA、NVMe、DMA、多队列调度、完整 async I/O syscalls、用户态异步接口、后台 writeback、完整存储驱动迁移或 broad networking。

## Capabilities

### New Capabilities

- `interrupt-driven-io-completion`: 定义内核块 I/O 请求的中断驱动完成模型、IRQ-safe 完成入口、等待线程唤醒、状态传播、超时/错误诊断和默认关闭验证边界。

### Modified Capabilities

- `block-io-request-layer`: 将现有请求层契约从“只提供同步提交与最终状态”扩展为可承载 pending 请求、完成状态更新和 scheduler-integrated 等待，同时保留现有同步消费者的确定性行为。
- `one-command-boot-debug`: 将 boot debug helper 和 xmake run target 的默认串口/诊断日志目录从 `log/` 改为 `logs/`，显式传入路径时仍尊重用户指定路径。
- `runtime-smoke-validation`: 将 runtime smoke matrix 的默认 artifact 和 per-case serial log 目录从 `log/` 改为 `logs/`，并保持验证记录可复现。

## Impact

- 影响子系统：块 I/O 请求层、块设备接口边界、scheduler wait queue/wakeup、IRQ handler 到完成路径的交接、page/buffer cache 消费边界，以及默认关闭的块 I/O runtime smoke。
- 影响工具与文档：`tools/boot_debug.py`、`xmake/run_targets.lua`、`tests/test_boot_debug.py`、runtime smoke validation 文档、boot layout/UEFI/memory validation 示例中显式引用的日志路径。
- 架构假设：当前目标仍为 x86_64 Legacy BIOS/MBR/exFAT 默认路径；APIC/SMP 基线可存在，但本变更不要求 UEFI runtime parity、非 x86 后端或新存储硬件后端。
- 内存与布局假设：请求和 completion 状态必须有界并由调用方或请求层明确拥有；IRQ 完成路径不得分配、释放、阻塞、访问文件系统或依赖 hosted runtime，不改变 boot handoff、链接地址、页表布局、IDT/syscall vector 或磁盘布局。
- 模拟器与工具链假设：实现时使用 xmake、x86_64-elf 工具链和可用的 QEMU/Bochs 验证默认关闭 smoke；若 emulator、ROM/display、磁盘镜像或 cross-binutils 不可用，验证记录必须明确跳过项和残余风险。
