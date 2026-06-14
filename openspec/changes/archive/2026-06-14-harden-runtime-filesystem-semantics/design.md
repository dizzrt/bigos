## Context

BigOS 已经具备只读 exFAT root、RAM-backed `/rw` 可写后端、fd/VFS、page/buffer cache、最小 libc、shell 和 packaged `/bin/*`。这些能力已经能支撑简单程序执行文件操作，但用户可观察语义仍需要在继续扩展 shell 和用户 ABI 前收紧：同一错误在 VFS/backend/syscall/libc 之间必须稳定映射，成功文件操作必须和后续 read/stat/list 保持一致，失败路径不得留下半成品目录项、inode、脏块或 fd 状态。

该 change 影响运行期文件系统路径，不改变早期 boot、linker、IDT、syscall vector、page-table layout、磁盘镜像分区布局或 x86_64 Legacy BIOS 启动约定。所有涉及路径解析、目录项更新、缓存装入/落盘和元数据查询的操作仍假设运行在允许阻塞、分配和同步块 IO 的普通进程上下文。

## Goals / Non-Goals

**Goals:**

- 为 fd/VFS、`/rw` 可写后端和 metadata 查询建立一致的用户可观察运行时语义。
- 统一只读 exFAT 与 RAM-backed `/rw` 在写入、创建、删除、rename、metadata 和目录枚举上的差异。
- 明确权限检查、owner/mode、目录写权限和容量限制在操作提交前生效。
- 保证成功路径对后续 read/stat/list 可观察，失败路径保持先前状态可解释。
- 用行为导向验证覆盖简单 C 程序和 shell 可观察路径，并记录环境不可用时的跳过风险。

**Non-Goals:**

- 不实现 journaling、跨重启持久化、磁盘分区承载的可写文件系统或 release-grade crash recovery。
- 不引入 ACL、xattr、硬链接、符号链接、设备节点、完整目录 rename 或完整 POSIX atomic replacement。
- 不实现广泛 file-backed `mmap`、async I/O、SMP 文件系统并发语义或新存储/设备驱动。
- 不扩展为完整 POSIX libc、完整 shell 语言、job control、mount namespace、`chroot` 或动态链接。
- 不修改 boot 地址、linker 地址、interrupt vector、syscall ABI、CR3 切换或磁盘镜像 layout。

## Decisions

- 决策：以 fd/VFS 为唯一用户可观察错误映射出口。
  理由：backend 内部状态枚举可以保持局部，但 syscall/libc/shell 只应依赖稳定负 errno。
  替代方案：让每个 backend 单独返回用户 errno。该方案会导致只读 exFAT、`/rw` 和 pipe/dir 对象错误映射漂移。

- 决策：把权限、容量、对象类型和后端可写性检查放在目录项或数据提交前。
  理由：先检查后提交能让失败路径保持文件数据、目录项、inode 元数据、fd offset 和 cache dirty 状态可解释。
  替代方案：提交后再补救回滚。该方案需要更复杂的事务模型，容易暗示 journaling 或 crash recovery。

- 决策：将 `/rw` 语义限定为当前运行期一致性。
  理由：默认承载介质仍是 RAM-backed 块设备，现有 raw image/MBR/exFAT boot asset 不应被可写路径修改。
  替代方案：把 `/rw` 落到磁盘分区。该方案会改变磁盘布局和持久化承诺，不适合本阶段。

- 决策：metadata 查询读取已提交的运行期状态，但不暴露稳定 inode 身份。
  理由：用户工具需要观察 type/size/mode/uid/gid 等 bounded 字段，而稳定对象编号、完整时间戳和 POSIX metadata database 仍超出范围。
  替代方案：直接暴露 backend inode 或 exFAT 标识。该方案会把内部结构固化为 ABI。

- 决策：目录枚举只验证最小有界记录和状态可见性。
  理由：Stage 31 需要确认 create/mkdir/unlink/rename 对目录可观察，但不需要完整 `DIR*`、offset cookie、排序或稳定快照。
  替代方案：实现完整 POSIX `readdir/getdents`。该方案会扩大 user ABI 和 shell/工具范围。

- 决策：最小目录枚举的调用方输出容量不足统一映射为 `-ERANGE`。
  理由：BigOS 已用 `ERANGE` 表示结果超出调用方缓冲区，目录枚举缓冲不足属于同类用户可观察边界；`EINVAL` 保留给非法参数，`ENOSPC` 保留给后端容量耗尽。
  替代方案：把缓冲不足映射为 `EINVAL` 会混淆“调用形态非法”和“结果放不下”；映射为 `ENOSPC` 会误导为文件系统或设备空间耗尽。

- 决策：受限 rename 在目标已存在时继续保守返回 `-EEXIST`。
  理由：当前 BigOS rename 只承诺同一 `/rw` 后端内的受限常规文件改名；保守失败能避免定义目标替换、目标 open fd 生命周期、替换回滚和 POSIX atomic replacement。
  替代方案：在本 change 引入 POSIX-like replacement。该方案会扩大语义和验证面，应作为后续独立 change 处理。

- 决策：权限硬化沿用当前最小 owner/mode 访问检查，不单独承诺完整目录 execute/search bit 语义。
  理由：本 change 目标是让现有权限拒绝在提交前稳定生效并返回 `-EACCES`，而不是建立完整 POSIX permissions model。
  替代方案：立即定义目录 search/traverse、read-dir、write-dir、root 例外和每级路径检查规则。该方案会扩大权限 ABI，应留给后续独立 change。

## Risks / Trade-offs

- [风险] 错误映射收紧可能暴露现有 backend 返回值不一致。→ 缓解：先建立统一映射表与行为断言，再逐个 backend 对齐。
- [风险] 失败路径回滚要求可能增加可写后端实现复杂度。→ 缓解：保持单操作提交边界，避免引入跨操作事务或 journaling。
- [风险] metadata 与目录枚举的可见性要求可能与 open-unlink/rename 引用生命周期交叉。→ 缓解：明确路径查询和 fd 查询分离，open file 引用归零前保持对象有效。
- [风险] QEMU/Bochs 或 cross toolchain 不可用会限制运行时验证。→ 缓解：记录跳过依赖和残余风险，并保留源码/行为断言作为最低验证。
- [风险] 用户可能误解 `/rw` 为持久文件系统。→ 缓解：spec、文档和验证说明统一强调 RAM-backed、当前运行期一致性，不承诺重启后保留。

## Migration Plan

- 先补齐 fd/VFS 与 backend 的错误映射和权限检查断言，保持既有默认 boot 行为不变。
- 再对 `/rw` 的 create/write/truncate/fsync/mkdir/unlink/rename/list/stat 组合语义逐项硬化。
- 然后补充用户态简单程序或 shell 可观察验证，覆盖只读 exFAT 与 `/rw` 差异。
- 如发现回归，回退对应操作的新增硬化路径，同时保留只读 exFAT mount、PID-1 init 和 `/bin/sh` 默认启动路径。
