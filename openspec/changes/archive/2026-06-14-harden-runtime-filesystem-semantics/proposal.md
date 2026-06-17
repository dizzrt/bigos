## Why

runtime filesystem semantics hardening 需要把 BigOS 已有 fd/VFS、RAM-backed `/rw` 和只读 exFAT 后端从“可用”推进到“用户态可依赖”：错误码、元数据、权限、目录项和只读/可写后端差异都应具有稳定、可验证的运行时语义。现在已有用户态、shell、最小 libc 和可写文件系统基础，正适合在继续扩展 shell 或 ABI 前收紧这些边界。

## What Changes

- 硬化路径型与 fd 型文件操作的用户可观察错误映射，覆盖缺失路径、非法对象类型、只读后端拒写、权限拒绝、容量耗尽、非法 fd、非法用户缓冲和不可阻塞上下文。
- 收紧 `/rw` 运行期文件、目录和元数据一致性，确保成功的 create/write/truncate/fsync/mkdir/unlink/rename 对后续 read/stat/list 可解释可观察，失败路径不发布半成品状态。
- 明确只读 exFAT 与 RAM-backed `/rw` 后端差异：只读后端不得被写请求、目录变更或元数据查询修改；`/rw` 只承诺当前运行期一致性，不承诺跨重启持久化。
- 补齐权限边界的行为要求，使 owner/mode、访问检查和目录写权限在可写后端操作前确定性生效。
- 扩展行为验证规划，覆盖用户态简单程序和 shell 可观察的成功/失败路径，同时记录工具链、模拟器和镜像依赖不可用时的跳过风险。
- 保持非目标边界：不引入 journaling、跨重启持久化、ACL、xattr、广泛 file-backed `mmap`、async I/O、硬/软链接、完整目录 rename、完整 POSIX atomic replacement、SMP 或新存储/设备驱动。

## Capabilities

### New Capabilities

- 无。

### Modified Capabilities

- `fd-vfs-shell`: 收紧运行时 fd/VFS 操作的错误映射、后端分派、目录行为、不可阻塞上下文拒绝和 open file 引用/offset 组合语义。
- `writable-filesystem`: 收紧 RAM-backed `/rw` 的权限检查、元数据/目录项一致性、失败回滚、只读后端差异和当前运行期一致性边界。
- `file-metadata-contract`: 收紧 stat/fstat 风格元数据与文件操作、目录变更、删除/rename 后路径和 fd 语义之间的一致性。

## Impact

- 受影响子系统：`kernel/core/fs` 的 VFS、exFAT 只读后端、RAM-backed 可写后端、page/buffer cache 集成点；`kernel/core/proc` 的 fd table、cwd、用户缓冲校验和 syscall 入口；`user` 的最小 libc、shell 和小工具消费路径。
- API/ABI 影响：稳定既有 BigOS 负 errno、bounded metadata、fd 操作和路径操作的可观察行为；不引入完整 POSIX ABI 或动态链接语义。
- 架构假设：当前目标仍是 x86_64 Legacy BIOS/MBR/exFAT 启动路径、单核内核、`int 0x80` 用户 ABI 和显式 CR3 切换；不改变 boot/linker/IDT/syscall vector/page-table 自映射地址。
- 内存与上下文假设：文件系统路径解析、缓存装入/落盘、目录项修改和元数据查询只在允许阻塞、分配和同步块 IO 的普通进程上下文执行。
- 磁盘与持久化假设：只读 exFAT boot assets 和现有 raw image 布局保持不变；`/rw` 默认承载在 RAM-backed 块设备，仅保证当前运行期一致性。
- 验证假设：优先使用源码/行为断言和 QEMU headless marker；若 x86_64 cross toolchain、QEMU/Bochs、ROM/display、raw image 或 serial oracle 不可用，验证记录为跳过而非通过。
