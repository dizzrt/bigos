## Why

当前 `/rw` 已具备有界可写文件、目录元数据、最小枚举、受限 rename 与 persistent clean-sync
基础，但 roadmap 的下一步需要把它从“单文件/少量操作可用”推进到“可写目录树可用”。
这能让 shell 与静态用户程序在同一运行期内创建和删除子目录、管理多个文件，并为后续文件扩展写、
稳定块分配、元数据持久化与回写路径成熟化提供清晰前置边界。

## What Changes

- 在现有 `/rw` 可写后端上补齐有界目录树语义：支持子目录创建、空目录删除、目录内多文件创建与删除。
- 收紧路径解析、父目录校验、目录项容量、名称长度、对象类型、权限和引用生命周期的确定性失败行为。
- 让目录枚举、metadata 查询、open/create、unlink、rmdir 和 shell/userland 工具在同一运行期内观察到一致的目录树状态。
- 保持现有 x86_64 Legacy BIOS 默认运行路径、只读 exFAT boot assets、UEFI boot spike、页表布局、syscall 向量和用户 ABI 不变。
- 不引入完整 POSIX 文件系统语义、跨挂载 rename、硬/软链接、完整目录 rename、完整 crash recovery、async I/O、动态链接或 SMP。
- 不把本变更扩大到文件扩展写、truncate、稳定块分配、journal/ordered-write 策略或 page/buffer cache 回写成熟化；这些仍由后续独立能力处理。

## Capabilities

### New Capabilities

- `writable-directory-tree`: 定义 `/rw` 有界可写目录树行为，包括目录创建/删除、多文件目录项管理、目录枚举一致性和确定性失败语义。

### Modified Capabilities

- `writable-filesystem`: 明确既有可写后端必须承载多层目录树，而不是只覆盖根目录下的最小文件/目录操作。
- `runtime-filesystem-maturity`: 扩展同一运行期内路径查找、fd I/O、metadata、目录枚举和 shell/userland 工具对目录树变更的一致可观察性。
- `persistent-writable-filesystem`: 要求 persistent clean-sync `/rw` 复用目录树行为，但仍只承诺 clean-sync 边界，不声称 crash recovery。

## Impact

- 受影响子系统：`kernel/core/fs` 的 VFS、bigfs/RAM-backed `/rw`、persistent clean-sync `/rw`、路径解析、目录项管理、metadata、目录枚举和缓存同步边界；`kernel/core/syscall` 与 `kernel/core/proc` 中已有文件/目录 syscall 分发和用户缓冲校验路径；`user` 下 shell、libc wrapper 与小型路径工具。
- 架构假设：当前交付目标仍是单核 x86_64；新增目录树代码应遵守现有 SMP preparation 的锁、内存顺序和不可阻塞上下文边界，但不启用真实 SMP。
- 内存与布局假设：不改变内核虚拟地址布局、用户地址空间 ABI、页表 self-map、boot handoff ABI、linker 地址或中断/syscall vector。
- 存储与磁盘假设：默认 Legacy BIOS/MBR/exFAT boot image 仍只承载只读 boot assets；`/rw` 可来自 RAM-backed 后端或已存在的 persistent clean-sync 后端，本变更不要求修改 MBR、分区表、exFAT 格式或新增磁盘驱动。
- 工具链与验证假设：实现期使用 xmake 与 x86_64-elf cross toolchain；运行时验证优先使用 QEMU headless 串口 marker，Bochs 可作为低层存储/启动交叉验证；不可用的工具链、模拟器、ROM/display 或磁盘镜像必须记录为跳过而非通过。
