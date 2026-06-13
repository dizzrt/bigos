## Context

当前 BigOS 的最小可用系统已经包含 `/rw` RAM-backed 可写运行时文件系统、cwd/相对路径、fd/VFS、syscall、libc wrapper、shell 和用户态路径工具。现有规格仍明确不支持 `rename`，因此简单用户程序只能通过创建、写入、枚举、删除等操作观察目录项变化，无法执行常见的“写入临时路径后切换名称”或“在同一运行期内调整文件名”的闭环。

本设计只扩展当前 x86_64 Legacy BIOS 正常运行路径中的文件系统和用户态消费层。它不改变 bootloader、链接地址、页表布局、CR3 切换、IDT/syscall vector、磁盘分区发现、exFAT 只读发现或 ATA PIO 驱动契约。

## Goals / Non-Goals

**Goals:**

- 在 `/rw` 可写后端内支持有界常规文件 rename，并让 cwd/相对路径与绝对路径走同一解析边界。
- 通过 VFS、syscall、libc 和小型用户态工具暴露一个一致、可观察、可验证的 rename 能力。
- 对失败路径提供确定性负 errno，并保证失败不会发布半成品目录项或破坏既有文件内容。
- 保持已打开文件引用语义稳定：路径名称变化不应让已有 fd 访问到无关对象，也不应提前释放仍被引用的 inode/data blocks。

**Non-Goals:**

- 不实现硬链接、符号链接、跨挂载 rename、完整目录 rename、完整 POSIX atomic replacement、`renameat`/`renameat2`、exchange、whiteout 或完整权限数据库。
- 不承诺跨重启持久化、journaling、crash consistency、广泛 file-backed `mmap`、async I/O、SMP、完整 POSIX shell 或完整 POSIX libc。
- 不修改当前 boot/image layout、MBR/partition/exFAT 只读路径、链接脚本、页表地址、syscall vector、IDT gate 策略或用户态寄存器 ABI。

## Decisions

- Decision: rename 范围限定为同一 `/rw` 后端内的常规文件目录项。
  Rationale: 当前 `/rw` 是运行期一致性的 RAM-backed 后端，适合先验证目录项替换和引用生命周期；跨挂载、只读 exFAT 和目录树搬移会显著扩大路径解析与一致性语义。
  Alternatives considered: 直接实现 POSIX-like rename；拒绝，因为会引入目标替换、目录语义、跨挂载错误和原子性承诺过多。

- Decision: VFS 提供路径型 rename 入口，先解析源父目录、目标父目录和最终名称，再委派给同一后端执行。
  Rationale: cwd、`.`/`..`、只读/可写后端分派和阻塞上下文边界已经集中在 fd/VFS 路径层；rename 复用该层可避免 libc 或工具自行 canonicalize。
  Alternatives considered: 只在 bigfs 后端暴露内部接口；拒绝，因为 syscall 和用户态工具仍需要统一错误映射和路径边界。

- Decision: 源路径和目标路径解析为同一父目录同一名称时返回成功 no-op。
  Rationale: 该行为不会产生目录项副作用，符合用户对自重命名的直觉，同时不要求实现完整路径 canonicalization 或跨不同字符串的同 inode 等价判断。
  Alternatives considered: 返回 `-EINVAL`；拒绝，因为这会把无害 no-op 暴露为错误，且不比受限 no-op 更易验证。

- Decision: 第一版不支持目标已存在的普通文件替换。
  Rationale: 目标已存在时返回确定性错误可以显著降低半成品状态风险，且仍满足 rename/move-to-new-name 的可观察能力；后续阶段可在已有验证基础上扩展替换语义。
  Alternatives considered: 允许常规文件覆盖；暂缓，因为需要更严格的双目录项更新顺序和失败回滚验证。

- Decision: syscall 以 append-only 方式增加 `SYS_RENAME`，参数为 `oldpath`、`newpath` 两个用户字符串指针，返回值沿用负 errno。
  Rationale: 追加号位保持既有 syscall 编号和寄存器约定稳定；两个 path 参数足以覆盖当前受限能力。
  Alternatives considered: 复用 `SYS_UNLINK` 或组合 open/unlink；拒绝，因为语义不可观察为单一目录项变更，也不利于验证失败原子性。

- Decision: libc 仅声明 `rename(const char*, const char*)` 的最小 wrapper 和必要 errno 翻译。
  Rationale: 简单 C 程序需要可移植的薄封装，但 BigOS 仍不是完整 POSIX libc；头文件必须只暴露已实现、有规格约束的接口。
  Alternatives considered: 同时引入 `renameat` 或 hosted `stdio` 文件流；拒绝，因为超出当前 freestanding 用户态边界。

- Decision: 用户态工具作为小型路径工具扩展，负责调用 libc rename wrapper 并报告 errno-based 错误。
  Rationale: shell 可观察能力应通过普通打包工具闭合，不需要扩大 shell 语法或内建命令集合。
  Alternatives considered: 只做内核 smoke；拒绝，因为 roadmap 要求 kernel-to-userland capability loop。

## Risks / Trade-offs

- [Risk] 目录项更新中途失败可能留下源/目标不一致状态 -> Mitigation: 后端先完成所有容量、权限、类型、父目录和目标存在性检查，再执行单一目录项移动；失败路径不得移除源项或发布目标项。
- [Risk] 已打开 fd 与 rename 后路径查找语义混淆 -> Mitigation: open file object 继续引用 inode/data blocks；rename 只改变目录项名称和父目录关系，不改变已有 fd 的底层对象。
- [Risk] syscall 用户路径拷贝引入 user memory 越界或内核地址读取 -> Mitigation: 沿用 VMA-backed 用户字符串拷贝、长度上限和 NUL 终止检查，非法输入返回确定性错误或走已有用户 fault 策略。
- [Risk] 只读 exFAT 或跨挂载路径被误改 -> Mitigation: VFS 在委派前确认源和目标属于同一可写后端；只读后端返回 `-EROFS`，跨挂载返回确定性错误。
- [Risk] 验证依赖本地交叉工具链和模拟器 -> Mitigation: tasks 中要求分层记录 xmake/source checks、QEMU/Bochs smoke 或明确跳过原因与残余风险。

## Migration Plan

- 先在规格和设计中固定保守语义，再实现 VFS/backend 内部 rename 入口。
- 追加 syscall 和 libc wrapper 时保持 ABI append-only，不重排既有 syscall 号位、不改变寄存器顺序。
- 打包用户态工具后通过 shell、绝对路径、相对路径、只读失败、目标存在失败和缺失路径失败验证行为。
- 若实现阶段发现目标替换或目录 rename 需要更大语义，应保持本 change 不做扩展，另起后续 change。
