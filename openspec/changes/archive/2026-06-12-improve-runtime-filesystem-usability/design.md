## Context

BigOS 已具备只读 exFAT 启动资产、RAM-backed `/rw`、fd/VFS、page/buffer cache、`int 0x80` syscall、进程 fd 表、最小用户态 libc 和 `/bin/sh`。Stage 24 的设计重点不是引入新的存储 backend，而是把这些已存在的层组合成简单 C 程序可依赖的有界运行时文件系统行为。

受影响子系统为 `kernel/core/fs`、`kernel/core/proc`、`kernel/core/syscall`、`user`、`include`、`docs` 和 `tests`。当前可运行环境仍假设 x86_64 Legacy BIOS/MBR/exFAT、现有磁盘镜像布局、RAM-backed 可写后端、xmake 与 `x86_64-elf-gcc`/`x86_64-elf-g++`；本设计不改变 boot 地址、linker 地址、IDT/syscall vector、CR3 切换、page-table layout 或磁盘分区契约。

数据流：

1. 用户程序通过最小 libc wrapper 调用 `open`/`read`/`write`/`lseek`/`fsync`/`mkdir`/`unlink`，并通过有界目录枚举接口观察目录项。
2. syscall 层按固定 `int 0x80` ABI 复制并校验用户 path/buffer，检查阻塞上下文守卫。
3. 进程 fd 表把 fd 路由到 open file object，VFS 根据挂载点进入只读 exFAT 或 RAM-backed 可写后端。
4. 可写后端通过 page/buffer cache 维护文件数据和元数据；写入后对后续读可见，`fsync` 或淘汰负责落盘到 RAM-backed 块设备。
5. 失败路径以负 errno 返回 syscall，再由 libc 翻译为 `errno` 与用户态失败哨兵。

## Goals / Non-Goals

**Goals:**

- 让简单静态 C 程序能在明确限制内可靠使用运行时文件创建、打开、读、写、seek、sync、目录创建、删除和最小目录枚举。
- 保持 fd/VFS、syscall、libc、shell 重定向和用户程序行为的组合一致性。
- 让只读 exFAT 与 RAM-backed `/rw` 的边界可观察：只读路径确定性拒写，可写路径确定性处理容量、权限、路径、引用和删除语义。
- 增加行为导向验证，覆盖单程序、跨 fd、fork/exec 继承、shell 重定向和错误码路径。
- 明确环境依赖：QEMU/Bochs、cross-toolchain、磁盘镜像或 `uv` 不可用时记录跳过原因和残余风险。

**Non-Goals:**

- 不提供跨重启持久化、磁盘分区承载、journaling、rename、硬/软链接、ACL/xattr、完整目录遍历、完整 `readdir/getdents` 兼容或完整 POSIX 权限模型。
- 不引入 broad file-backed `mmap`、async I/O、SMP、新存储驱动、新 boot backend 或 UEFI runtime parity。
- 不引入 hosted `FILE` 流、动态链接、共享库、完整 POSIX libc 或完整 shell/job-control/terminal 语义。
- 不修改 syscall vector、既有 syscall 号位、寄存器 ABI、interrupt EOI 语义、page-table layout、linker script 或 boot handoff。

## Decisions

- 运行时可写状态继续限定在 RAM-backed `/rw`。理由：这能提升用户可见文件行为而不改变磁盘镜像、MBR/exFAT 或 ATA PIO 启动路径。替代方案“将可写后端落到现有磁盘分区”会引入持久化、一致性和镜像布局风险，超出本阶段。
- 使用现有 fd/VFS 作为唯一用户可见文件入口。理由：简单 C 程序、shell 重定向、dup 和 exec 继承都已经以 fd 为组合边界；绕过 fd 增加专用 API 会扩大 ABI 面。替代方案“直接暴露文件系统私有 syscall”会破坏 VFS 后端边界。
- syscall ABI 采用 append-only 或既有号位语义扩展。理由：保持现有用户程序和验证路径兼容。替代方案“重排文件 syscall”会破坏 ABI，且不符合当前稳定化阶段目标。
- 写后读一致性以 page/buffer cache 为运行期真相，`fsync` 和淘汰负责写回 RAM-backed 块设备。理由：符合现有缓存设计，能验证 `fsync` 但不承诺真实磁盘持久化。替代方案“同步直写所有文件操作”会简化一致性但削弱缓存 smoke 覆盖并增加阻塞成本。
- 删除语义采用有界 UNIX-like 行为：`unlink` 先移除目录项，使新的路径查找不可见；若仍有 open fd 引用，inode 与数据块整体延迟到最后一个 open fd 关闭后再释放，已打开文件对象在引用归零前保持可读写。理由：该语义与 fd 引用模型、shell 重定向和临时文件场景组合稳定，并避免 fd 仍可访问时数据块被提前复用。替代方案“拒绝删除已打开文件”更简单，但限制简单 C 程序常见用法；替代方案“只延迟 inode 或只延迟数据块回收”更容易引入生命周期不一致。
- 本阶段提供最小目录枚举，但只作为有界运行时可用性接口：返回目录项名称和基础类型，限制单次返回大小和目录项数量，不承诺完整 POSIX `DIR*`/`struct dirent`/offset cookie 兼容。理由：简单 C 程序和 shell/验证路径需要观察 `mkdir`、文件创建与 `unlink` 的目录结果；替代方案“仅靠路径查找验证”可用性不足。
- libc 只提供薄 wrapper、常量和 errno 翻译。理由：本阶段目标是系统调用可用性，不是 hosted stdio。替代方案“实现 `fopen`/`FILE` 缓冲流”会扩大 libc 范围并引入额外缓冲一致性问题。
- 验证以行为断言为主、源码/构建/运行时分层，并优先复用或扩展现有 userland smoke 程序承载文件系统行为覆盖。理由：复用现有打包、启动、串口/控制台观察和环境跳过记录路径，避免在本阶段新增一套维护成本更高的独立工具；若复用导致用例过大，再拆出小型专用用户程序。

## Risks / Trade-offs

- [Risk] RAM-backed `/rw` 易被误解为持久化文件系统 -> 通过规格、文档和错误/验证说明反复标明只保证运行期一致性。
- [Risk] 删除已打开文件的引用生命周期可能暴露 use-after-free 或泄漏 -> 在任务中要求 fd 引用计数、inode/data block 生命周期和 reap/close 路径审查。
- [Risk] `fsync` 在 RAM-backed 设备上“落盘”语义有限 -> 将语义限定为写回到当前运行期块设备并通过淘汰后读回验证，不宣称跨重启。
- [Risk] syscall 用户缓冲校验遗漏会导致内核读写非法用户地址 -> 任务要求覆盖 path、读源、写目标和 fd 数组等 VMA-backed 校验。
- [Risk] 文件操作可能在不可阻塞上下文触发分配或同步 IO -> fd/VFS、syscall 和缓存层均保留阻塞上下文守卫，失败走确定性错误或诊断路径。
- [Risk] 规格增量跨多个 capability，归档时可能遗漏同步 -> 每个 proposal 中列出的 modified capability 均提供 delta spec，并在 tasks 中加入 OpenSpec strict validation。

## Migration Plan

1. 先补齐规格与文档边界，确认 `/rw`、fd/VFS、syscall、libc、shell 和 runtime validation 的契约一致。
2. 分层实现或审查内核路径：可写后端和缓存一致性、fd/VFS 引用语义、syscall 用户缓冲校验、errno 映射。
3. 分层实现或审查用户态路径：libc wrapper/headers、复用或扩展现有 userland smoke 程序、shell 重定向错误报告。
4. 增加行为验证：源码/构建检查、默认关闭 runtime smoke、可选 QEMU/Bochs headless 验证；环境不可用时记录跳过。
5. 回滚策略：保持所有运行时验证默认关闭；若实现路径不稳定，可保留只读 exFAT 和现有 `/rw` smoke，不提升用户文档承诺。

## Resolved Notes

- 本阶段提供最小目录枚举，但仅覆盖目录项名称和基础类型的有界枚举，不扩展为完整 POSIX 目录 API。
- 文件系统行为验证优先复用或扩展现有 userland smoke 程序；只有当复用导致用例耦合过大时，才拆出小型专用用户程序。
- 删除已打开文件后的空间回收采用整体延迟释放：目录项先从路径空间移除，新的路径查找不可见；inode 与数据块保持到最后一个 open fd 关闭后再释放。这样最符合 fd 引用生命周期，也能避免 fd 仍可访问时数据块被提前复用导致 use-after-free 或数据串扰。
