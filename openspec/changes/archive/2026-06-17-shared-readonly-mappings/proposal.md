## Why

当前 file-backed read mapping 已能为用户进程按需物化只读文件页，但跨进程复用仍停留在进程局部视角。为支撑更多静态用户程序、降低重复 text/data 页占用，并为后续更成熟的用户态运行环境留出基础，需要把只读文件页提升为可被多个进程安全共享的 bounded 能力。

该 change 承接地址空间与 mmap 完善主线中的共享只读映射目标：在不启用 SMP、不引入完整 POSIX `mmap` 的前提下，让多个进程可共享只读 text 与数据页，并通过既有 SMP preparation 的 TLB invalidation boundary 表达权限与映射变更。

## What Changes

- 为只读 file-backed 映射增加跨进程共享物化页能力：同一只读 backing object、页对齐文件偏移与只读权限组合可以复用同一物理页。
- 为 VFS/backends 补齐稳定 object id，并使用独立 bounded 共享目录管理跨进程只读物化页。
- 改造 ELF loader，使兼容的只读 text/rodata segment 也通过共享只读 file-backed 页路径复用；writable data/bss 仍保持私有语义。
- 扩展 fork/exec/teardown/unmap 路径中的共享只读页引用计数与生命周期规则，避免深拷贝或提前释放仍被其他地址空间引用的页。
- 将共享只读页的 PTE 发布、权限收紧、unmap 与 teardown 纳入现有 TLB invalidation boundary，当前单核仍执行本地 invalidation，未来 SMP 可扩展为 cross-CPU shootdown。
- 增加默认关闭的行为验证，覆盖显式 file-backed mapping 共享、exec text/rodata 共享、写访问失败、unmap/exit 后另一进程仍可读取、以及权限/TLB 边界不变。
- 不实现 writable shared mapping、写回 file-backed mapping、完整 POSIX `MAP_SHARED`、动态链接、共享库装载、真实 SMP、APIC/IPI 或跨 CPU TLB shootdown。

## Capabilities

### New Capabilities

- `shared-readonly-mappings`: 定义 BigOS 有界共享只读 file-backed 页能力，包括共享键、引用计数、跨进程生命周期、权限边界、TLB invalidation 约束与验证要求。

### Modified Capabilities

- `file-backed-read-mapping`: 将既有只读 file-backed 私有映射扩展为可复用共享只读物化页，同时保留非 writable、非完整 POSIX `mmap` 的边界。
- `address-space-lifecycle`: 补充 unmap、teardown、fork/exec 对共享只读页引用计数与释放顺序的要求。
- `smp-preparation`: 明确共享只读映射的权限更新与释放仍必须通过既有 TLB invalidation boundary 表达，不启用真实多核。

## Impact

- 受影响子系统：`kernel/core/proc` 的 VMA、page fault、fork、exec、ELF loader、unmap/protection-change、process teardown；`kernel/core/fs` 的 VFS object identity、file-backed mapping 与 page/buffer cache 读路径；`kernel/mm` 的页引用计数、PTE 权限发布与 TLB invalidation helper；`kernel/core/syscall` 的映射相关 syscall dispatch 边界；`user` 的最小映射验证程序或 smoke consumer。
- API/ABI 影响：不改变 `int 0x80` vector、寄存器 ABI、既有 syscall 号位、用户低半区布局、boot handoff、linker address、direct map、recursive self-mapping 或磁盘布局。若需要暴露新 flag 或权限组合，必须 append-only 并保持旧调用确定性行为。
- 架构与运行假设：当前交付目标仍是 x86_64 Legacy BIOS 默认路径、单核、同步 I/O、freestanding C++17/C17；UEFI runtime parity、其他 ISA、AP startup 与真实 SMP 均不属于本 change。
- 内存与工具链假设：共享页只覆盖只读、页对齐、page/buffer cache 可读取的 file-backed 页面；物理页生命周期必须可引用计数；验证依赖 x86_64 cross toolchain、OpenSpec CLI，以及可用时的 QEMU headless 串口 smoke。
