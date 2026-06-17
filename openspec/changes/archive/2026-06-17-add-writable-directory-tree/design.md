## Context

BigOS 当前已有单核 x86_64、有界用户态、VFS、RAM-backed `/rw`、persistent clean-sync
`/rw`、page/buffer cache、cwd/相对路径、metadata、最小目录枚举和受限常规文件 rename。
这些能力已经能支撑根目录下的文件创建、读写、删除，以及部分子目录 smoke，但 roadmap
接下来要求把 `/rw` 明确提升为有界可写目录树：用户程序和 shell 能在 `/rw` 下创建子目录、
在目录内创建/删除多个文件，并删除空目录。

本设计影响 `kernel/core/fs` 为主，向上触及 syscall 分发、进程 fd/cwd 状态、用户缓冲校验、
libc wrapper、shell 和路径工具；向下复用已有块缓存和 RAM-backed/persistent bigfs 后端。
它不跨越 boot、IRQ、页表或 linker 边界，不改变 `int 0x80` ABI 的既有寄存器约定，不改变
Legacy BIOS/MBR/exFAT boot image 布局。

## Goals / Non-Goals

**Goals:**

- `/rw` 支持有界多层目录树，至少覆盖创建子目录、删除空目录、目录内多文件创建与删除。
- 路径解析在绝对路径、cwd 相对路径、`.` 和 `..` 上保持现有有界规则，并对非目录中间组件、过长路径、过长名称确定性失败。
- VFS、bigfs、metadata、目录枚举、shell/path tools 和 userland smoke 对目录树变更观察一致。
- 删除已打开常规文件继续保持现有 open-fd 引用语义；删除目录仅允许空目录且不破坏 cwd/fd 可解释性。
- RAM-backed 与 persistent clean-sync `/rw` 复用同一目录树语义；persistent 后端只扩大 clean-sync 可见性，不扩大到 crash recovery。
- 失败路径不发布半成品目录项、inode 或 fd；空间、权限、对象类型、非法用户缓冲和 IO 失败返回稳定 errno。
- 新增或改动代码遵守 freestanding C/C++、可阻塞上下文边界和既有 SMP preparation 约束。

**Non-Goals:**

- 不实现完整 POSIX filesystem、完整 `rmdir`/`unlinkat`/`renameat`、硬链接、软链接、mount namespace、ACL/xattr、完整目录 rename 或跨挂载 rename。
- 不实现文件扩展写之外的新文件容量模型、truncate、稳定块分配策略、journal、ordered-write 策略或完整 crash recovery。
- 不引入 async I/O、宽泛块设备框架、SMP 启用、动态链接或完整 POSIX libc。
- 不改变 boot handoff ABI、磁盘分区布局、exFAT 只读发现路径、页表布局、中断向量或 syscall 寄存器 ABI。

## Decisions

### 1. 在 bigfs 内补齐目录树，而不是新增独立文件系统

复用现有 bigfs inode、目录项、block cache 和 RAM/persistent 后端，让目录树成为 `/rw`
能力的自然延伸。这样可以保持 VFS mount 边界和 `/rw` 路径前缀不变，也能让 persistent
clean-sync 后端复用同一套路径、metadata、枚举和引用语义。

替代方案是新增一个目录树后端并并行挂载。该方案会重复 VFS glue、缓存同步和路径工具行为，
并扩大 persistent clean-sync 后续整合成本，因此不采用。

### 2. 目录删除采用独立 `SYS_RMDIR` 入口

常规文件删除继续走现有 `unlink` 语义；目录删除作为单独能力处理，要求目标为目录、目录为空、
父目录可写，成功后移除目录项并按引用状态延迟释放目录 inode/数据块。本变更新增独立
`SYS_RMDIR`，并同步提供有界 libc wrapper 和用户态路径工具；syscall 编号只能追加，不能重排既有
编号或改变 `int 0x80` 寄存器 ABI。

替代方案是让 `unlink` 删除空目录。该行为和现有 `unlink` 对目录返回 `EISDIR` 的边界冲突，
也会让用户工具更难表达对象类型错误，因此不采用。

### 3. 目录项提交使用“小事务式”顺序

创建目录或文件时先完成路径和权限校验，再预留 inode、目录槽和必要数据块，最后写入 inode
与目录项；任一步失败都回滚已预留资源并保持父目录可解释。删除空目录时先确认目标目录为空且
无不可释放引用，再清除父目录项，随后释放目录 inode 和数据块。对于 persistent 后端，成功
`fsync` 或显式同步前仍只承诺 clean-sync 边界。

替代方案是直接逐步写入并依赖后续扫描修复。该方式会引入未定义半成品状态，且没有 journal
支撑，不适合当前 bounded clean-sync 目标。

### 4. 目录枚举只承诺有界可观察性

目录枚举必须能观察成功创建/删除后的目录项，并返回基础类型和名称；它不承诺 POSIX `DIR*`
完整兼容、稳定快照、排序、`.`/`..` 条目或跨调用 cookie。VFS/syscall 层继续限制单次返回条目数
和名称长度，非法 fd、非目录 fd、过小缓冲和非法用户缓冲返回确定性错误。

替代方案是定义完整 POSIX `readdir`。这会要求更强 offset cookie、并发稳定性和 libc surface，
超出当前 bounded userland。

### 5. 上下文边界沿用现有可阻塞文件系统规则

目录树操作会分配内核对象、访问 block cache、可能同步块 IO，因此只能从普通可阻塞进程上下文
进入。IRQ、scheduler critical section、preemption-disabled 区域或其它不可阻塞路径必须确定性拒绝
或进入既有诊断路径，不能发布目录项、fd 或 dirty cache 状态。

### 6. 引入 deleted-directory cwd 语义

当空目录被 `SYS_RMDIR` 删除但仍被某个进程的 cwd 引用时，删除操作可以移除父目录项，使新的路径
查找不再找到该目录；同时 cwd 持有的目录引用进入 deleted-directory 状态。该状态下，进程的
`getcwd`、相对路径解析和 `chdir("..")` 必须保持确定性：可以返回文档化错误，也可以解析到仍可解释
的父目录，但不能访问已释放对象或静默指向新创建的同名目录。目录 inode 和数据块释放必须延迟到最后
一个 cwd/open-directory 引用释放之后。

替代方案是对被 cwd 引用的目录直接返回 busy 类错误。该方案实现更简单，但会让目录删除语义过于保守，
并不能验证后续真实文件系统需要的“目录项消失但引用对象仍可解释”的生命周期边界，因此不采用。

### 7. 复用 persistent writable smoke 的双阶段 marker

persistent clean-sync 目录树验证复用已有 persistent writable smoke 的双阶段结构：第一阶段在同一
persistent test disk 上创建目录树、创建/删除文件和空目录、执行同步并发射写入阶段 marker；第二阶段
使用同一磁盘重新启动，验证同步后的目录树状态并发射验证阶段 marker。该验证只证明 clean-sync 边界，
不声明 crash recovery、journal replay 或未同步 dirty 状态持久化。

替代方案是新增独立 persistent 目录树脚本。该方案会重复磁盘准备、双启动和 marker 管理逻辑，增加维护
成本，因此不采用。

## Risks / Trade-offs

- [Risk] 当前 inode、目录项和文件大小上限较小，目录树测试可能很快耗尽容量。→ Mitigation:
  明确容量为有界语义，覆盖 ENOSPC 回滚测试，并把更大容量或块分配成熟化留给后续能力。
- [Risk] persistent clean-sync 后端在无 journal 下遇到中途失败可能产生复杂状态。→ Mitigation:
  本变更只要求失败前不发布半成品和成功同步后的 clean-reboot 可见性，不声明 crash recovery。
- [Risk] deleted-directory cwd 语义可能让相对路径和 `getcwd` 行为变复杂。→ Mitigation:
  cwd/open-directory 必须持有显式目录引用；目录项可删除，但目录 inode/data block 释放延迟到最后引用释放，
  deleted cwd 下的 `getcwd`、相对 lookup 和 `chdir("..")` 必须返回确定性结果或文档化错误。
- [Risk] 新增目录删除 syscall 可能触及 ABI surface。→ Mitigation:
  `SYS_RMDIR` 只能追加编号，不重排既有编号，并同步 libc/header/tool/smoke。
- [Risk] shell 和 libc wrapper 可能先于内核语义暴露不完整行为。→ Mitigation:
  任务拆分要求先完成 VFS/bigfs 语义，再接用户态 wrapper/tool，最后用 userland smoke 和 shell transcript 验证。

## Migration Plan

1. 在 bigfs/VFS 内补齐目录树核心语义和确定性错误，不改变默认挂载路径。
2. 追加 `SYS_RMDIR`、libc wrapper 和路径工具，并保持既有 syscall 编号不变。
3. 为 cwd/path state 引入 deleted-directory 引用语义，确保目录项删除后引用对象仍可解释且延迟释放。
4. 扩展默认关闭 smoke：覆盖多层目录、多文件、空目录删除、非空目录拒绝、deleted-directory cwd、只读后端拒绝和容量耗尽回滚。
5. 复用 persistent writable smoke 的双阶段 marker，验证目录树 clean-sync 后跨 clean reboot 可见；工具链或模拟器不可用时记录跳过原因。

Rollback 策略是关闭新增 smoke 和用户工具入口，保留既有 `/rw` 文件操作路径；任何修改 boot image、
ABI 编号重排或 exFAT 读路径的变更都应在 review 中阻断。

## Resolved Questions

当前无待决设计问题。已决策：新增独立 `SYS_RMDIR`；引入 deleted-directory cwd 语义；复用已有
persistent writable smoke 的双阶段 marker 验证目录树 clean-sync 行为。
