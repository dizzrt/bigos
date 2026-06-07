## Why

BigOS 目前只能在 Legacy BIOS bootloader 路径中使用一次性的 exFAT 读取逻辑加载 `boot.bin` 和 `kernel`，内核启动后没有可复用的块设备或文件读取能力。阶段 7 需要把只读磁盘访问下沉到 kernel runtime，为后续从文件系统加载 ELF 用户程序、测试资源和更多内核服务做准备。

## What Changes

- 新增内核态块设备只读抽象，提供按 LBA 扇区读取的 bounded API，并以 ATA PIO 作为首个后端。
- 新增最小只读基础 FS 层，支持从 MBR 分区表自动发现 exFAT 分区，挂载一个块设备上的 exFAT volume 并按绝对路径读取文件。
- 新增 exFAT 只读解析能力，覆盖 boot region、cluster heap、root directory、目录项集合、文件名匹配、连续文件读取，以及完整 bounded FAT 链跟随。
- 新增默认关闭的构建/运行 smoke，用固定镜像中的 `/boot/fs_smoke.txt` 验证 kernel 内部可读取 exFAT 文件并输出稳定 COM1 marker。
- 保持 bootloader 现有 MBR/exFAT DBR/extended DBR/ELF kernel 加载路径不变；新能力只在进入内核后提供。

非目标：

- 不实现写入、删除、目录修改、权限模型、缓存一致性、page cache 或完整 VFS。
- 不实现 AHCI/NVMe/USB 存储、DMA、异步 I/O 或块设备热插拔。
- 不实现完整 exFAT 规范全集，例如完整 upcase table 语义、timezone/时间戳处理、allocation bitmap 修改、TexFAT 或损坏卷修复。
- 不改变 boot fixed addresses、BootInfo handoff ABI、kernel higher-half 链接地址或现有用户态 flat image smoke。

## Capabilities

### New Capabilities

- `block-device-read`: 定义内核态只读块设备模型、扇区读取契约、错误边界和首个 ATA PIO 后端。
- `exfat-read-filesystem`: 定义基础只读 FS/exFAT 挂载与文件读取能力，包括 MBR exFAT 分区发现、路径查找、目录项解析、FAT 链跟随和 bounded file read API。

### Modified Capabilities

- 无。

## Impact

- 影响子系统：`src/drivers` 新增块设备/ATA 读取后端，`src/kernel` 新增基础 FS/exFAT runtime，必要时新增 `include/bigos` 或 `include/drivers` 公共头。
- 影响构建：`xmake.lua` 新增默认关闭的 `fs_smoke` 或等价开关；smoke 仍通过 `xmake f ...` 持久化并由 `xmake run bochs-sdl2` / `xmake run bochs` 启动。
- 影响工具：`tools/boot_debug.py` 或测试镜像生成逻辑可复用现有 exFAT raw image 布局，提供用于 kernel FS smoke 的固定测试文件。
- 影响诊断：失败路径使用统一 panic/marker 机制；runtime smoke 输出稳定 `BIGOS_FS_EXFAT_READ_*` marker。
- 架构假设：单核 x86_64、Legacy BIOS raw disk image、Bochs 可观测 COM1 serial、`x86_64-elf-gcc` freestanding toolchain、磁盘后端先按 ATA primary master PIO 约束实现。
- 内存假设：direct map、kernel virtual memory、buddy/slab 已可用；块/FS 初始化只在普通非 IRQ 上下文运行，不承诺 IRQ handler-safe 或 sleepable allocation 语义。
