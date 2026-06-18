## Why

当前 `/rw` 已具备有界创建、写入、目录树和 clean-sync 持久化基础，但文件增长、截断和数据块分配仍需要形成更稳定的契约，才能支撑多文件持久写入并降低后续一致性与回写工作的返工风险。

本变更补齐常规文件从空文件到多块文件的扩展写、收缩/扩展截断，以及失败时不污染既有状态的稳定块分配语义，继续保持 BigOS 的单核、同步、bounded userland 边界。

## What Changes

- 为 `/rw` 常规文件定义有界文件增长能力：追加写、带 hole 的偏移写、跨块写入和多块读取必须在当前文件大小、块数和缓存容量限制内可解释。
- 为 `/rw` 常规文件定义截断能力：收缩必须释放不再拥有的数据块，扩展必须形成确定的零填充读取语义，失败不得发布部分 size 或块映射。
- 为 RAM-backed 和 persistent clean-sync `/rw` 后端定义稳定块分配语义：分配、回滚、释放和复用必须避免数据泄漏、重复释放、块别名和只读 exFAT 状态污染。
- 将成功的扩展写与截断结果纳入现有 fd、metadata、目录树和 persistent clean-sync 可见性契约。
- 增加 default-off 行为验证，覆盖文件增长、截断、容量耗尽、块复用和 clean reboot 后同步状态读回。
- 不引入完整 POSIX `ftruncate`/`mmap` 写回、稀疏文件承诺、journal、完整 crash recovery、async I/O、SMP 或新的存储/设备 backend。

## Capabilities

### New Capabilities

- `stable-file-growth`: 覆盖 `/rw` 常规文件扩展写、截断、零填充读取、稳定块分配、失败回滚、块释放复用和验证边界。

### Modified Capabilities

- `writable-filesystem`: 将扩展写、截断和块分配失败纳入现有 `/rw` 运行期一致性、权限/容量失败和 fd 可见性要求。
- `persistent-writable-filesystem`: 将同步后的扩展文件、截断后文件大小和稳定块映射纳入 persistent clean-sync `/rw` 跨 clean reboot 可见性要求。
- `file-metadata-contract`: 要求 path/fd metadata 在成功扩展写和截断后反映有界文件大小，并在失败时保持旧状态。
- `page-buffer-cache`: 要求扩展写和截断后的脏块、释放块和重新读取路径保持缓存/后端状态一致，不把失败写回报告为 durable success。

## Impact

- 受影响子系统：`kernel/core/fs` 的 VFS、`bigfs`、persistent `/rw` 后端、page/buffer cache，以及相关 syscall fd 路径。
- 受影响用户态：最小 libc 文件大小/截断 wrapper、shell 或小型验证程序可按需要扩展，但不要求完整 POSIX 工具链。
- 验证影响：新增或扩展 default-off filesystem smoke/source checks，优先覆盖 QEMU Legacy BIOS 路径；若 emulator、cross toolchain 或持久测试盘不可用，验证记录必须明确跳过原因与残余风险。
- 架构和布局假设：继续以 x86_64 Legacy BIOS/MBR/exFAT 默认运行路径为目标；不修改 boot handoff ABI、磁盘启动资产布局、页表布局、IDT/syscall vector、syscall number 排列或 UEFI backend parity。
