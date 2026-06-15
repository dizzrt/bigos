## Why

Stage 44 需要在前序 libc、文件系统、进程、终端和 VM 合同稳定后选择一个大型用户可见扩展。根据 `roadmap.md`，持久可写文件系统能优先提升 BigOS 的实际可用性、开发工作流和长时间运行行为，因此本 change 将当前 RAM-backed `/rw` 运行期可写能力推进为有界的磁盘持久化能力。

## What Changes

- 为现有 `/rw` 可写后端增加一个显式选择的持久化承载模式，使成功同步后的文件、目录和 metadata 能跨重启重新挂载并被观察。
- 扩展块设备、块缓冲缓存和 VFS/可写后端之间的写入、同步、挂载既有卷、最小用户态 mkfs 格式化新卷和失败降级合同。
- 保持现有只读 exFAT boot assets、Legacy BIOS/MBR 启动路径和 x86_64-only 交付目标不变；持久化区域不得破坏现有 boot/kernel/user 程序打包路径。
- 为可复现验证定义同一脚本连续两次启动同一个独立持久测试磁盘镜像的 clean reboot smoke、只读资产隔离检查、容量/权限失败检查、同步后缓存淘汰再读检查，以及不可用工具链或模拟器时的记录要求。
- 非目标：不引入 dynamic linking/shared libraries、journaling、完整 POSIX filesystem、硬/软链接、ACL/xattr、完整目录 rename、POSIX atomic replacement、broad file-backed `mmap`、async I/O、SMP、多架构 backend、UEFI runtime parity 或广泛存储驱动。

## Capabilities

### New Capabilities

- `persistent-writable-filesystem`: 定义有界持久可写文件系统能力，包括持久卷识别、格式化/挂载、同步后跨重启可见性、失败降级、只读资产隔离和验证要求。

### Modified Capabilities

- `writable-filesystem`: 将当前 RAM-backed `/rw` 语义扩展为可选择持久后端，并保留运行期 RAM-backed 模式作为非持久 fallback/验证路径。
- `runtime-filesystem-maturity`: 修改 Stage 41 “为持久存储做准备但不实现”的边界，使其接纳 Stage 44 的持久化语义，同时继续区分运行期一致性和跨重启持久性。
- `page-buffer-cache`: 扩展同步/淘汰合同，要求持久后端上的 `fsync` 和缓存写回具备跨重启可验证的落盘语义。
- `block-device-read`: 扩展已有块设备写接口要求，明确持久后端写入路径、只读设备拒绝写和同步轮询失败行为可被文件系统依赖。

## Impact

- 影响子系统：`kernel/core/fs` 的 VFS、可写后端、挂载/路径解析、metadata 与 fd I/O；块缓冲缓存；块设备读写抽象；构建工具生成的独立持久测试磁盘承载路径；最小用户态 mkfs 工具和默认关闭 smoke。
- API/ABI 影响：保持 `int 0x80` syscall ABI、现有 fd/VFS 用户可见接口和当前用户程序静态运行模型不变；新增格式化入口应限制为最小用户态 mkfs，不声明完整 POSIX `mkfs`/`mount`/设备管理工具链。
- 架构假设：当前交付目标仍为 x86_64 Legacy BIOS/MBR/exFAT；UEFI backend、其他 ISA 和 SMP 不属于本 change。
- 内存与地址布局假设：不改变内核 higher-half 地址、boot handoff ABI、页表 self-mapping、用户地址空间 ABI 或 CR3 切换合同。
- 磁盘布局假设：持久化区域使用构建工具生成的独立持久测试磁盘；不得隐式覆盖 MBR、exFAT boot 分区或 packaged user binaries；旧版本、未知版本或未来版本持久卷必须拒绝挂载，不做自动迁移或自动格式化。
- 模拟器与工具链假设：验证优先使用 xmake、x86_64-elf-gcc、QEMU headless；Bochs 用于 Legacy BIOS/ATA PIO 交叉验证。工具链、ROM/display、磁盘镜像或模拟器不可用时必须记录为跳过验证及残余风险。
