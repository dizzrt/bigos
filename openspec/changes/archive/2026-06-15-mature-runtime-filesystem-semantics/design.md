## Context

BigOS 当前已经具备只读 exFAT boot assets、RAM-backed `/rw` 可写后端、fd/VFS、page/buffer cache、`stat`/`fstat` 风格 metadata、cwd 相对路径、最小 libc wrapper、shell 和小型路径工具。runtime filesystem maturity 的目标不是新增一个全新的持久存储系统，而是把这些现有能力组合成普通静态 C 程序可以依赖的运行期文件系统语义。

当前 `/rw` 由内存页承载，并在启动时重新格式化；它可以通过 buffer cache、`fsync` 和后端写入路径验证当前 boot session 内的一致性，但不具备跨重启持久化、磁盘布局保护、mount-existing、journal/replay 或 fsck 恢复语义。底层 ATA PIO 已有同步写与 flush 原语，但 runtime filesystem maturity 不把它们提升为持久可写文件系统承诺。

主要受影响边界包括 `kernel/core/fs`、syscall fd/path 入口、进程 fd 生命周期、用户态 libc errno wrapper、shell 工具错误展示和默认关闭 runtime smoke。当前交付目标仍是 x86_64 Legacy BIOS/MBR/exFAT；不改变 boot 地址、linker 地址、IDT/syscall vector、page-table layout、磁盘镜像布局或 boot asset 发现契约。

## Goals / Non-Goals

**Goals:**

- 让 read-only exFAT 和 RAM-backed `/rw` 的后端差异在 fd/VFS、metadata、cwd-relative path、libc wrapper 和 shell 工具中一致可观察。
- 稳定 `/rw` 当前运行期内的 create、write、read、lseek、fsync、mkdir、unlink、restricted regular-file rename、minimal directory enumeration 和 metadata 行为。
- 明确成功操作对后续 path lookup、fd I/O、stat/fstat、目录枚举、dup/fork/exec inherited fd 的可见性。
- 明确失败操作的状态保持：不发布半成品目录项/inode/data block，不推进失败写入的 offset，不破坏 fd table、cwd、cache dirty state 或无关后端状态。
- 扩展行为验证，覆盖普通用户程序、libc errno、shell 工具和运行时 smoke 能观察到的成功/失败组合。

**Non-Goals:**

- 不实现跨重启持久化，不把 `/rw` 挂载到真实磁盘分区或保留 LBA 区域。
- 不修改现有 MBR、exFAT boot image、bootloader、kernel image 或只读 boot assets。
- 不引入 journaling、mount-time repair、fsck、崩溃恢复、完整 POSIX atomic replacement rename 或完整目录 rename。
- 不引入 symlink、hard link、mount namespace、`chroot`、完整 `realpath`、完整 POSIX `DIR*`/`struct dirent`、stable inode identity、完整 timestamp、ACL/xattr 或 device node。
- 不引入 async I/O、broad storage/device drivers、broad file-backed `mmap`、dynamic linking、SMP 或新 ISA backend。

## Decisions

### Decision 1: runtime filesystem maturity 只承诺当前运行期一致性

`/rw` 继续作为 RAM-backed writable backend。成功写入、同步、目录变更和 metadata 更新必须在同一 boot session 内对后续操作一致可见，但文档、规格和验证不得暗示重启后仍存在。

选择原因：当前 `bigfs` 启动时重新分配内存并格式化，直接做持久挂载会扩大到磁盘布局、分区保护、格式升级和恢复策略。先稳定运行期语义可以为后续 persistent storage 提供更清晰的行为基础。

备选方案：在 runtime filesystem maturity 直接把 `bigfs` 挂到 ATA 磁盘区域。该方案被推迟，因为它需要先定义 boot image 保护、existing filesystem mount、superblock 版本、损坏处理和 reboot-cycle 验证。

### Decision 2: 以 fd/VFS 作为用户可见语义汇聚点

所有用户态可见文件操作通过共享路径解析、后端分派、fd table 引用生命周期和 errno 映射收敛。backend 可以保持简单，但 VFS/syscall 层必须保证用户看到的成功/失败和状态变化一致。

选择原因：普通程序和 shell 不应依赖内部 backend enum、调试字符串或不同入口的偶然差异。fd/VFS 是 read-only exFAT、`/rw`、pipe/dup、metadata 和 cwd-relative path 的共同边界。

备选方案：分别在每个 syscall 或 user tool 内修补差异。该方案容易造成 open/write/stat/readdir/rename 的错误码和状态可见性不一致。

### Decision 3: 失败路径优先保持可解释状态

容量耗尽、权限拒绝、非法路径、非法 fd、非法用户缓冲、只读后端写请求、backend I/O 失败和不可阻塞上下文调用都必须返回确定性错误。失败不得发布半成品对象，不得推进不应推进的 offset，不得破坏已打开 fd 或无关目录项。

选择原因：runtime filesystem maturity 的价值在于让小程序可以依赖错误契约；这比扩大 POSIX 表面积更重要。

备选方案：允许部分成功或 best-effort mutation。该方案会让后续持久化阶段难以定义 recovery 和 replay 语义。

### Decision 4: path metadata 和 fd metadata 分离

目录项被 unlink 或 restricted rename 后，path metadata 按当前目录可见性解析，fd metadata 按 live open file object 解析，直到最后一个 fd 引用关闭。

选择原因：这与现有 open-file reference 方向一致，能让用户程序在删除/rename 已打开文件时得到稳定行为，同时不暴露 stable inode identity。

备选方案：unlink/rename 时立即让已打开 fd 失效。该方案实现更简单，但会破坏 fd 引用生命周期和 dup/fork/exec 继承的一致性。

### Decision 5: 新增专用 runtime filesystem maturity smoke

runtime filesystem maturity 增加一个专用、默认关闭的 filesystem maturity smoke，用来覆盖 read-only exFAT 成功路径、`/rw` 创建/写入/读取/metadata/目录枚举/删除/rename 成功路径，以及只读写、容量、权限、非法 fd、非法 buffer、错误目录对象、缓冲不足等失败路径。现有 writable/userland smoke 可以继续覆盖基础回归，但不作为 runtime filesystem maturity 组合语义的唯一验证入口。环境缺失时记录 skipped，不声称通过。

选择原因：runtime filesystem maturity 是成熟化阶段，必须验证跨层组合行为；但 BigOS 仍是有界研究内核，不应用 POSIX 全量测试套件定义范围。

备选方案：只保留现有 smoke marker。该方案无法捕获 fd/path/backend/metadata/libc/shell 组合语义回归。

### Decision 6: 容量耗尽验证优先自然填满 `/rw`

容量耗尽验证优先通过普通文件系统操作自然填满 RAM-backed `/rw` 的 inode、目录项或数据块上限触发，而不是为 runtime filesystem maturity 缩小 `bigfs` 常量或引入全局分配失败注入。内核分配失败、cache pinning 等难以自然触发的路径可以由 source-level checks 或局部测试补充，但不作为用户态容量语义的主路径。

选择原因：自然填满验证直接覆盖真实 bounded 后端容量、提交顺序和 errno 映射，不改变生产常量，也避免 fault injection 与正常路径产生偏差。

备选方案：缩小 `bigfs` 上限会让验证配置偏离默认运行时；全局注入分配失败能覆盖更多错误路径，但容易引入与具体实现强绑定的测试钩子。

### Decision 7: 目录枚举规定稳定输出顺序

runtime filesystem maturity 规定最小目录枚举必须按稳定后端顺序返回目录项：`/rw` 使用目录 slot 顺序，read-only exFAT 使用后端目录遍历顺序，fd/VFS 和 libc wrapper 不得重新随机化或依赖未初始化顺序。该顺序只要求在相同目录状态下稳定可复现，不声明字典序排序、POSIX cookie、完整快照或完整 `DIR*` 兼容。

选择原因：稳定顺序能让 shell 工具、用户程序和 smoke 验证可复现，同时仍保持 bounded subset，不把 runtime filesystem maturity 扩大成完整 POSIX 目录流。

备选方案：继续不承诺顺序会保留更多实现自由，但会让验证和用户可观察行为更脆弱。

## Risks / Trade-offs

- [Risk] 增加组合验证可能暴露现有 backend 局部语义不一致 → Mitigation: 先以小型用户程序和 source-level checks 覆盖核心组合，再逐步扩展 smoke。
- [Risk] 容量/权限失败路径可能需要重构提交顺序 → Mitigation: 将检查前置，目录项/inode/data block 提交保持显式阶段化，失败时保持旧状态可解释。
- [Risk] 目录枚举稳定顺序容易被误读为 POSIX 排序或完整目录流支持 → Mitigation: 规格只承诺 backend traversal/slot order 的可复现性，不声明字典序、cookie、快照、`DIR*`、stable inode、完整 timestamp 或完整 `stat` 兼容。
- [Risk] `fsync` 语义可能被误解为跨重启持久化 → Mitigation: 明确 `fsync` 只保证当前 RAM-backed block device/cache 的运行期一致性，不承诺 reboot 后保留。
- [Risk] 不做持久化会推迟实际开发工作流收益 → Mitigation: 把 runtime filesystem maturity 的稳定语义作为 persistent clean-sync /rw storage persistent storage 的前置输入，后续再选择磁盘布局和挂载策略。

## Migration Plan

1. 保持现有 x86_64 Legacy BIOS/MBR/exFAT 启动和只读 exFAT mount 不变。
2. 在 `bigfs`、VFS、syscall、process fd lifecycle、libc wrapper 和 user tools 中收敛运行期语义和错误映射。
3. 增加 source-level checks 和专用默认关闭 filesystem maturity runtime smoke，验证普通程序可观察组合路径。
4. 更新相关 OpenSpec 规格和必要文档，明确 `/rw` 仍为 RAM-backed current-session writable filesystem。
5. 若实现导致启动或 userland smoke 回归，回滚对应成熟化代码，不需要迁移磁盘数据，因为 runtime filesystem maturity 不引入持久状态。

## Resolved Questions

- runtime filesystem maturity 使用专用、默认关闭的 filesystem maturity smoke；现有 writable/userland smoke 只作为基础回归补充。
- 容量耗尽验证优先通过自然填满 RAM-backed `/rw` 的真实 bounded 容量触发；分配失败等难以自然触发路径由 source-level checks 或局部测试补充。
- 目录枚举规定稳定后端顺序：`/rw` 为目录 slot 顺序，exFAT 为后端遍历顺序；不声明字典序、POSIX cookie、完整快照或完整 `DIR*` 兼容。
