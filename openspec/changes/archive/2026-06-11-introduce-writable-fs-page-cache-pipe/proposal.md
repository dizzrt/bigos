## Why

time and identity capability（时间与身份，已完成）已交付文件 owner/mode 访问判定纯原语 `bigos::cred::may_access`（见 [cred.cc](kernel/core/proc/cred.cc)），但至今没有任何可写文件系统或 IPC 通道去消费它：当前 I/O 栈是**单向只读**的——同步只读 ATA PIO、只读 exFAT 挂载、只读 fd/VFS 壳层、`SYS_WRITE` 仅能写控制台、无块缓存、无 `pipe`/`dup`。这意味着用户进程无法持久化任何数据，`fork`+`exec`（fork/exec process capability）出来的进程也无法用管道串联，shell（shell 与用户态组合能力）的重定向与管道因此完全不可能。路线图可写文件系统与 pipe/dup foundation 要求在demand paging capability（按需分页）与time and identity capability（时间与身份）之上补齐**通用 I/O 语义**：先做 page/buffer cache 地基，再在其上做可写文件系统与 `pipe`/`dup`，并把 owner/mode 权限模型**从一开始**就建进可写路径，而非事后再补。趁现在 I/O 面仍小（单核、同步、`int 0x80`、只读栈边界清晰）时把可写语义立成最小但正确的地基，可复用现有 block/exFAT/fd-VFS/cred 结构，避免日后在更复杂的并发与缓存一致性模型上补写路径。

## What Changes

- 新增内核 page/buffer cache（`bigos::mm::bcache` 或 `bigos::fs::bcache`）：以「块设备 + 块号」为键、缓存固定大小块（与现有扇区/簇读路径对齐）的脏/干净页，提供 `get`/`put`/`mark_dirty`/`sync` 接口；读路径优先命中缓存，未命中经现有 block 层装入；写路径写入缓存并标脏，按写回（write-back）或显式 `sync`/`fsync` 落盘。缓存容量有界，溢出时按确定性策略（如 LRU 近似）淘汰干净页，绝不在 IRQ/不可阻塞上下文落盘。
- 在现有只读 ATA PIO block 驱动之上新增**块设备写路径**（block-device write，ATA PIO LBA28/LBA48 write-sectors），经 buffer cache 统一进出；保持现有只读读路径与 MBR/exFAT 发现契约不变。
- 新增最小**可写文件系统**后端，与现有只读 exFAT 挂载并存：在 VFS 之上提供 `O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC` 打开、文件 `write`、`lseek`、文件创建/截断、目录项创建（`mkdir`）与删除（`unlink`）的最小子集，文件 inode 携带 owner/mode 元数据。具体后端（exFAT 写 vs 更简单的 FS，如 ext2 或块/RAM 支持的最小可写 FS）的取舍在 design 中决策，默认选择对 freestanding 单核内核最可控、风险最低、能复用 buffer cache 与现有 block 层的方案。
- 扩展 fd/VFS 壳层：`vfs::File` 支持可写 `write` op 与 `lseek`，`open_absolute` 接受可写 flags 与 `O_CREAT` 的 owner/mode 入参；`vfs::Status` 与 fd 层语义覆盖写失败、ENOSPC、EROFS（只读后端拒绝写）、权限拒绝（EACCES）等确定性失败。
- 新增 `pipe` IPC：`SYS_PIPE` 创建一对相连的读端/写端 `File`（内核环形缓冲、有界），`write` 满时与 `read` 空时按阻塞语义（复用现有 wait queue/blocking 原语）在可阻塞进程上下文挂起/唤醒，写端全关闭后读端读到 EOF、读端全关闭后写端写返回 `EPIPE`（本阶段以错误返回代替 `SIGPIPE`，或可选向写者投递 `SIGPIPE`——见 design）。新增 `SYS_DUP`/`SYS_DUP2` 复制 fd 以支持 shell 重定向。
- 在 `int 0x80` ABI 末尾**仅追加**新固定号：`SYS_PIPE`、`SYS_DUP`、`SYS_DUP2`、`SYS_LSEEK`、`SYS_FSYNC`、`SYS_MKDIR`、`SYS_UNLINK`（最终集合与号位在 design/specs 固化）；并扩展既有 `SYS_OPEN` 接受可写/创建 flags、`SYS_WRITE` 支持写入文件 fd（不再仅控制台）。寄存器 ABI、现有号位、向量布局、DPL 设置、「syscall 不发 EOI」均不变。
- 把 `cred::may_access`（文件 owner/mode 判定）接成可写 FS open/write/create 的**实际权限强制点**：root 全放行，否则按 owner/group/other 与请求访问类型判定，拒绝返回确定性 `-EACCES`；判定逻辑零改动，仅新增接线点。只读 exFAT 后端对写请求一律 `-EROFS`。
- 新增默认关闭的验证开关（如 `writable_fs_smoke` / `pipe_smoke`，定义 `BIGOS_WRITABLE_FS_SMOKE` / `BIGOS_PIPE_SMOKE`），发射固定 COM1/VGA marker（如 `BIGOS_WRITABLE_FS_PASSED`/`_FAILED`、`BIGOS_PIPE_PASSED`/`_FAILED`），覆盖「写后读回一致」「`fsync` 落盘后缓存淘汰仍可读回」「`O_CREAT` 建文件 + owner/mode 强制」「只读后端写被 `EROFS` 拒绝」「pipe 写读跨进程 + 阻塞/唤醒 + EOF/EPIPE」「`dup`/`dup2` 共享 offset」等路径；保留现有 smoke 矩阵不删除。
- **非破坏性**：不改变 `int 0x80` 寄存器 ABI 约定（仅在末尾追加新号、扩展既有号语义且不改其号位）、IDT/向量布局、DPL、页表自映射地址、CR3 切换约定、higher-half/direct-map/`KVMEM_BASE` 布局、MBR/分区/exFAT 只读发现契约、boot/磁盘镜像布局；`#PF` 与外部 IRQ EOI 语义不变；不引入 SMP/锁；现有只读 exFAT 路径行为不变。

## Capabilities

### New Capabilities
- `page-buffer-cache`: 内核块缓冲缓存——以（块设备, 块号）为键缓存固定大小块的脏/干净状态，读命中/未命中装入、写入标脏、写回/显式 `sync`/`fsync` 落盘、有界容量与确定性淘汰（仅淘汰干净页或先回写脏页）、严格的可阻塞上下文边界（落盘与装入只在允许阻塞与同步块 IO 的进程上下文进行，绝不在 IRQ/不可阻塞上下文落盘）、以及缓存与底层块设备一致性的确定性失败语义。
- `writable-filesystem`: 最小可写文件系统能力——与只读 exFAT 并存的可写后端，支持 `O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC` 打开、文件 `write` 与 `lseek`、文件创建/截断、目录项 `mkdir`/`unlink` 的最小子集，inode 携带 owner/mode 元数据并以 `cred::may_access` 为实际访问强制点，所有写经 buffer cache，只读后端对写请求确定性 `-EROFS`，写满/无空间/越界/权限拒绝等确定性失败语义，且不引入完整 POSIX 文件语义（无硬/软链接、无 rename、无 mmap 文件映射、无 ACL/xattr）。
- `pipe-ipc`: 管道与 fd 复制能力——`pipe` 创建一对内核有界环形缓冲连接的读/写端 `File`，写满阻塞、读空阻塞（复用 wait queue/blocking 原语，仅在可阻塞进程上下文），写端全关 -> 读端 EOF、读端全关 -> 写端 `EPIPE`（或可选 `SIGPIPE`），`dup`/`dup2` 复制描述符并共享底层 `File`（offset/读写端引用计数共享），fork/exec 下的引用计数与 close-on-exec 语义，以及非法 fd/越界/容量耗尽的确定性失败语义。

### Modified Capabilities
- `syscall-entry`: `int 0x80` ABI 在末尾追加 `SYS_PIPE`/`SYS_DUP`/`SYS_DUP2`/`SYS_LSEEK`/`SYS_FSYNC`/`SYS_MKDIR`/`SYS_UNLINK`（最终集合在 specs 固化）；`SYS_OPEN` 扩展接受可写/创建 flags 与 owner/mode 入参、`SYS_WRITE` 扩展支持写入文件/管道 fd（不再仅控制台）；返回值经 rax 回写，其余 syscall 号、寄存器约定与「syscall 不发 EOI」不变；写/管道相关 syscall 在分配或进入同步块 IO/阻塞前 MUST 检查调度阻塞守卫。
- `fd-vfs-shell`: `vfs::File`/`FileOperations` 新增 `write` 与 `lseek` op；`open_absolute` 接受可写 flags、`O_CREAT` 的 owner/mode 入参；`vfs::Status` 覆盖只读后端拒写（`-EROFS`）、无空间（`-ENOSPC`）、权限拒绝（`-EACCES`）；fd 层支持 pipe-backed `File`、`dup`/`dup2` 引用计数共享与 exec 继承/close-on-exec；现有只读 open/read/close 语义与只读 exFAT 行为不变。
- `block-device-read`: 在现有只读 ATA PIO 读路径之上新增块设备**写**路径（write-sectors），统一经 buffer cache 进出；保持现有只读读契约、扇区大小、LBA 解析与 MBR/exFAT 发现不变；写路径定义确定性 IO 失败语义（设备错误 -> `-EIO`，不破坏缓存一致性）。
- `process-identity-permissions`: `cred::may_access`（文件 owner/mode 访问判定）从「仅纯判定、无强制点」升级为「可写 FS open/write/create 的实际强制点」；判定语义本身不变（root 放行、按 owner/group/other 匹配权限位、非法输入拒绝），仅新增其被实际调用的接线点。

## Impact

- 受影响子系统：新增 buffer cache 模块（`kernel/mm/bcache` 或 `kernel/core/fs/bcache`：块缓存装入/标脏/写回/淘汰）、可写 FS 后端（`kernel/core/fs/*`：可写 inode/目录项/写路径，或新增子目录）、`kernel/core/fs`（`vfs::File` 写/lseek op、可写 open）、`kernel/core/ipc` 或 `kernel/core/fs`（pipe 环形缓冲与读/写端 File）、`kernel/drivers/block`（ATA PIO 写）、`kernel/core/syscall`（新增/扩展 syscall 分支）、`kernel/core/proc`（fd 表 dup/dup2、pipe fd、exec 继承/close-on-exec、exit/reap 关闭管道端）、`kernel/core/proc`（`cred::may_access` 接线）。
- 受影响代码：新增 buffer cache 头与实现、可写 FS 源、pipe 源；[vfs.h](include/bigos/fs/vfs.h) 与 [vfs.cc](kernel/core/fs/vfs.cc)（写/lseek op、可写 open、新 Status）；[syscall.h](include/bigos/syscall.h) 与 [syscall.cc](kernel/core/syscall/syscall.cc)（追加号位、扩展 open/write 分支）；ATA PIO 块驱动（新增写）；[proc.h](include/bigos/proc.h) 与 [proc.cc](kernel/core/proc/proc.cc)（fd dup/管道/exec 继承/exit 关闭）；[cred.h](include/bigos/cred.h) / [cred.cc](kernel/core/proc/cred.cc)（`may_access` 接线，判定逻辑不变）；[errno.h](include/bigos/errno.h)（补齐 `EROFS`/`ENOSPC`/`EACCES`/`EPIPE`/`ESPIPE` 等错误码，保持单一来源）。
- 构建/验证：`xmake.lua` 新增默认关闭开关（如 `writable_fs_smoke` / `pipe_smoke`）；QEMU headless serial-marker smoke 与源码契约/行为断言测试（沿用behavior assertion validation baseline 启动的行为断言测试轨道）；涉及块设备写与缓存一致性时，在可用环境下补充 Bochs 或 QEMU+Bochs 交叉验证；clang/clangd 辅助静态检查。
- 假设：x86_64 单核、同步、`int 0x80`、`InterruptFrame` ABI 与向量/DPL 布局不变；现有只读 ATA PIO/exFAT/fd-VFS 与 MBR/分区/磁盘镜像布局不变；buffer cache 落盘与可写 FS/管道阻塞只在可阻塞进程上下文进行，绝不在 IRQ/不可阻塞上下文落盘或阻塞；demand paging capability（按需分页）、growable process and fd table capability（可增长进程/fd 表）、fork/exec process capability/16.5/17（fork、时间与身份、信号）已就位；可写 FS 后端与 buffer cache 的容量、块大小、目录/inode 上限有界且在 design 明确；Bochs/QEMU 经 `tools/boot_debug.py` 验证；Python 验证经 `uv run` 执行。
- 非目标：完整 POSIX 文件系统语义（硬链接/软链接/`rename`/`stat` 全字段/`fstat`/`fcntl` 全标志/`O_APPEND` 之外的全部 open 标志）、文件 mmap 与页缓存共享映射（file-backed mmap 仍是后续工作）、目录遍历 `readdir`/`getdents` 完整语义（仅最小创建/删除）、多挂载命名空间与 `mount`/`umount` syscall、日志/崩溃一致性（journaling）、`fsck`、配额、ACL/xattr、命名管道（FIFO）/`mknod`/socket、`SIGPIPE` 之外的完整管道信号语义、SMP 下的缓存一致性与锁、写性能优化（仅保证正确性与有界性）。这些留给shell 与用户态组合能力 与后续工作。
