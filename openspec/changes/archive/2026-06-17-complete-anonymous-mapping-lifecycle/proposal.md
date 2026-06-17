## Why

BigOS 已经具备 VMA-backed 的受限匿名映射、lazy materialization、`fork`/COW 和 file-backed read mapping 基线，但匿名映射仍缺少完整生命周期：用户程序不能按有界语义解除映射，也不能在已有 VMA 上收紧或调整权限。完善匿名映射生命周期是继续推进地址空间与 `mmap` 成熟度的下一步，同时为后续共享只读映射和更成熟 libc 小程序提供稳定边界。

## What Changes

- 扩展受限匿名映射能力，明确匿名 VMA 的创建、局部/完整解除映射、权限变更和失败回滚语义。
- 新增 bounded `munmap`-like 内核路径和 syscall 表面，仅支持用户低半区、页对齐、完全 VMA 覆盖或可拆分的匿名区间，不实现完整 POSIX `munmap`。
- 新增 bounded `mprotect`-like 内核路径和 syscall 表面，仅允许在已存在的匿名 VMA 上调整受支持权限，拒绝 W+X、file-backed 写权限升级和跨不兼容 VMA 的范围。
- 让 unmap/protection change 与现有 VMA materialization accounting、用户页表 unmap、COW 记账、TLB invalidation 准备边界和 safe teardown 规则保持一致。
- 更新用户态 wrapper、shell/小程序可观察路径和默认关闭 smoke，使匿名映射 map/unmap/protect 的行为可复现验证。
- 非目标：不实现完整 POSIX `mmap`/`munmap`/`mprotect`，不支持 `MAP_FIXED` 覆盖、shared writable mapping、file-backed writable mapping、swap、async I/O、SMP TLB shootdown、动态链接或完整 POSIX libc。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `vma-user-memory-api`: 扩展受限匿名映射生命周期，新增匿名 VMA 解除映射、权限变更、VMA 拆分/合并、materialization accounting 更新和验证要求。
- `syscall-entry`: 扩展 bounded 用户内存 syscall 表面，覆盖匿名映射解除映射和权限变更的 ABI、错误返回与 user-buffer 安全边界。
- `address-space-lifecycle`: 明确主动 unmap 与 teardown 的资源释放顺序、COW/shared frame 引用处理、页表回收和 TLB invalidation 准备边界。

## Impact

- 影响子系统：`kernel/core/proc` 的 VMA/进程内存 API、`kernel/mm` 用户页表 unmap/protection helpers、`kernel/core/syscall` 分发、`include/bigos` syscall/进程接口、`user` freestanding libc wrapper 和相关 smoke/tests。
- 架构假设：当前交付目标仍为 x86_64 单核 Legacy BIOS baseline；x86_64 UEFI boot backend 仍是非运行时等价 spike；本 change 不引入新 ISA 或 SMP。
- 内存布局假设：保留用户低半区、higher-half kernel、direct map、`KVMEM_BASE`、recursive self-mapping、syscall vector 和现有 CR3 切换边界；不移动 boot/linker 地址或页表自映射地址。
- 模拟器和工具链假设：构建以 `xmake` 与 `x86_64-elf-gcc/g++` 为主；运行验证优先 QEMU headless 串口 marker，Bochs 作为可用时的低层交叉验证；Python 辅助检查通过 `uv run ...` 执行。
- 磁盘布局假设：继续使用当前 Legacy BIOS/MBR/exFAT raw image 和已有用户程序打包路径；不修改 boot sector、磁盘分区格式、UEFI/OVMF、virtio/AHCI/NVMe 或持久文件系统格式。
