## Context

BigOS 当前 I/O 栈是单向只读的，可写语义全缺：

- 块层 [block_device.h](include/drivers/block/block_device.h) 只有 `ReadSectorsFn read_impl` 与 `read_sectors`，`BlockDevice` 无写入口；ATA PIO 驱动只实现读扇区。
- 文件系统 [vfs.h](include/bigos/fs/vfs.h) 的 `FileOperations` 只有 `read`/`close`，`File` 有 `offset`/`ref_count`/`readable`/`close_on_exec` 但无写 op、无 `lseek`；`open_absolute` 接受 flags 但只读，`Status` 已含 `Unsupported`/`BlockError`/`WouldBlock` 等。`OPEN_WRONLY`/`OPEN_RDWR`/`OPEN_CREAT`/`OPEN_TRUNC` 常量已定义但未实现。
- syscall ABI [syscall.h](include/bigos/syscall.h) 最大号是 `SYS_SIGRETURN = 19`；`SYS_WRITE = 2` 当前仅写控制台，`SYS_OPEN = 5` 仅只读。新增号只能在末尾追加，寄存器 ABI 冻结，syscall 路径不发 EOI。
- time and identity capability 已实现并测试文件 owner/mode 访问判定纯原语 `cred::may_access`（见 [process-identity-permissions/spec.md](openspec/specs/process-identity-permissions/spec.md)），但无强制点。
- 阻塞原语（wait queue、wake-one/all、timeout、blocking-context guard）、按需分页、fork/COW、可增长 fd 表均已就位；fd 层已有 close-on-exec 与 exec 继承语义雏形（见 [fd-vfs-shell/spec.md](openspec/specs/fd-vfs-shell/spec.md)）。

约束：freestanding、单核、同步、无 libc；落盘与阻塞只能在可阻塞进程上下文进行，绝不在 IRQ/不可阻塞上下文落盘或阻塞；磁盘镜像/MBR/分区/exFAT 只读发现契约与 boot 布局不可变。本阶段把三件事一并立成最小但正确的地基：page/buffer cache、可写 FS、pipe+dup。

## Goals / Non-Goals

**Goals:**

- 提供内核 buffer cache：以（块设备, 块号）为键缓存固定大小块、维护脏/干净状态，读优先命中、写入标脏、写回/显式 `fsync` 落盘，容量有界、确定性淘汰，落盘只在可阻塞上下文。
- 在 ATA PIO 块层新增写扇区路径，经 buffer cache 统一进出，保持只读读契约不变。
- 提供最小可写文件系统后端（与只读 exFAT 并存）：可写 open（`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`）、文件 `write`、`lseek`、文件创建/截断、目录项 `mkdir`/`unlink`，inode 携带 owner/mode，并以 `cred::may_access` 为实际访问强制点。
- 提供 `pipe` IPC（有界环形缓冲、读空/写满阻塞、EOF/EPIPE）与 `dup`/`dup2`（共享底层 `File`），支撑 shell 重定向与管道。
- 扩展 fd/VFS 与 syscall ABI（仅末尾追加号、扩展既有 open/write 语义），全部经默认关闭开关与源码契约/行为断言验证，失败语义确定性、无 panic（内核态 fault 除外）。

**Non-Goals:**

- 完整 POSIX 文件语义：硬/软链接、`rename`、`stat`/`fstat` 全字段、`fcntl` 全标志、`readdir`/`getdents` 完整遍历、`O_APPEND` 之外的全部 open 标志。
- file-backed mmap 与页缓存共享映射、多挂载命名空间与 `mount`/`umount`、journaling/崩溃一致性、`fsck`、配额、ACL/xattr。
- 命名管道（FIFO）/`mknod`/socket、完整 `SIGPIPE`/管道信号语义之外的内容。
- SMP 下的缓存一致性与锁、写性能优化（仅保证正确性与有界性）。
- 不改 `int 0x80` 寄存器 ABI、现有 syscall 号位、IDT/向量/DPL、页表/CR3/地址布局、外部 IRQ/异常 EOI 语义、MBR/分区/exFAT 只读发现与磁盘镜像布局。

## Decisions

### 决策 1：buffer cache 以（设备, 块号）为键，块大小对齐扇区，落盘只在可阻塞上下文

新增 `bigos::fs::bcache`（落在 `kernel/core/fs/bcache`，与块/FS 同层，避免 mm 依赖 FS）。缓存条目 `BufferBlock { BlockDevice* dev; uint64_t block_no; uint8_t* data; bool dirty; bool valid; uint32_t ref_count; /* LRU 链接 */ }`，块大小取 `DEFAULT_SECTOR_SIZE`（512）的固定倍数（design 固定为 1 扇区/块起步，便于与现有读路径对齐），缓存数据页用 `alloc_kernel_pages`。接口：`get(dev, block_no) -> BufferBlock*`（命中返回，未命中分配并经 `block::read_sectors` 装入）、`put(block)`（释放引用）、`mark_dirty(block)`、`sync(block)` / `sync_all()`（把脏块经新写扇区路径回写）。

- 理由：块缓存是可写 FS 与「写后立即读回一致」的地基；以（设备, 块号）为键是最小且标准的设计。读优先命中缓存可让 FS 元数据/数据访问统一走一条路径。
- 容量与淘汰：固定上限 N 块（编译期常量，有界）。淘汰优先选 `ref_count==0` 的干净块；若需淘汰脏块，先 `sync` 回写再复用（回写在可阻塞上下文进行）。无可淘汰块时 `get` 返回 `nullptr` -> 上层确定性 `-ENOMEM`/`-ENOSPC`，绝不阻塞死等、绝不在 IRQ 上下文落盘。
- 上下文：`get`（未命中装入）与 `sync`（落盘）执行同步块 IO，MUST 只在可阻塞进程上下文调用；调用前由 syscall 层检查现有调度阻塞守卫（复用 fd/VFS 既有边界，见 [fd-vfs-shell/spec.md](openspec/specs/fd-vfs-shell/spec.md) 的阻塞上下文边界要求）。
- 备选：(a) mm 层 page cache 与 VM 共享映射 —— 引入 file-backed mmap 复杂度，超范围，否决（列为非目标）；(b) 无缓存直写 —— 无法保证「淘汰后读回一致」且每次 FS 元数据访问都打盘，否决。

### 决策 2：ATA PIO 写扇区，扩展 `BlockDevice` 写入口，只读读契约不变

在 [block_device.h](include/drivers/block/block_device.h) 的 `BlockDevice` **追加** `WriteSectorsFn write_impl`（可为 `nullptr` 表示只读设备），新增 `block::write_sectors(dev, lba, count, src, src_len)`，在 ATA PIO 驱动实现 LBA28/LBA48 write-sectors + flush cache。`write_impl == nullptr` 的设备写入返回 `Unsupported`，由上层映射为 `-EROFS`。

- 理由：追加字段不破坏现有只读读路径与 `read_impl` 布局；`write_sectors` 与 `read_sectors` 对称，buffer cache 的 `sync` 唯一经此落盘。
- 失败行为：设备超时/错误 -> `BlockStatus::DeviceTimeout`/`DeviceError` -> 上层 `-EIO`，缓存块保持 dirty（不丢数据、不破坏一致性），不 panic。
- 硬件/顺序：写扇区是端口 IO + 等待 BSY/DRQ + 写数据 + flush，须与现有读时序一致地处理 BSY/DRDY/DRQ 轮询；本阶段单核同步，无并发块 IO。审查 ATA PIO 状态机以确保写不破坏后续读。
- 备选：直接在 FS 里发 ATA 命令 —— 破坏块层抽象，否决。

### 决策 3：可写 FS 后端选型——RAM 支持的最小可写 FS（`bigfs`）作为可写后端，与只读 exFAT 并存

本阶段可写后端选择一个**最小自定义可写 FS（暂称 `bigfs`）**，其超级块/inode/目录项/数据块布局简单、固定块大小、经 buffer cache 读写，挂载在与只读 exFAT 不同的挂载点（如 `/rw` 或 root 之下的子树）。后端介质既可由磁盘上一个保留分区/区域承载，也可由内存盘（RAM-backed BlockDevice）承载用于验证；具体介质由 design 固定为「优先 RAM-backed BlockDevice 做默认验证介质，磁盘分区承载为可选」，以避免改动现有磁盘镜像/MBR/exFAT 布局。

- 理由：(a) 在只读 exFAT 上加写需实现 exFAT 簇分配/FAT 链更新/目录项写，复杂且高风险，易破坏现有只读发现契约；(b) ext2 功能完整但元数据多、实现量大。一个最小自定义可写 FS 能以最小代码覆盖「创建/写/截断/lseek/mkdir/unlink + owner/mode」语义，复用 buffer cache 与块写路径，且不触碰现有 exFAT 只读路径。RAM-backed BlockDevice 作为默认验证介质可让 smoke 在不改磁盘镜像的前提下跑通，磁盘分区承载留作可选演进。
- 布局（最小）：超级块（魔数、块大小、inode/数据位图位置、根 inode 号）；inode（mode/owner uid/gid/size/直接块指针，固定个数直接块，无间接块或仅一级间接，有界文件大小）；目录是「定长目录项数组」的特殊文件（name + inode 号）。所有结构经 buffer cache 块读写。
- 与 VFS 集成：`bigfs` 实现 `FileOperations`（read/write/close）与 vnode/inode 解析、目录创建/删除；只读 exFAT 后端保留原 `read`/`close`，其 `write` op 为「返回 `-EROFS`」。
- 备选：(a) exFAT 写 —— 风险/工作量高、易破坏只读契约，否决为本阶段方案（列为后续可选）；(b) ext2 —— 元数据复杂、超最小目标，否决。
- 失败行为：inode/数据块位图耗尽 -> `-ENOSPC`；目录项满 -> `-ENOSPC`；越界写/非法 inode -> `-EINVAL`；块 IO 失败 -> `-EIO`；权限拒绝 -> `-EACCES`；只读后端写 -> `-EROFS`。绝不在失败路径破坏已落盘元数据一致性、不 panic。

### 决策 4：VFS `File`/`FileOperations` 扩展写与 lseek，open 接受可写 flags + owner/mode

在 [vfs.h](include/bigos/fs/vfs.h)：`FileOperations` 追加 `WriteOp write` 与 `LseekOp lseek`（追加字段，只读后端可设 write=拒绝、lseek=普通 offset 调整）；`File` 追加 `bool writable`；`open_absolute` 签名扩展为接受写/创建 flags 与 `O_CREAT` 时的 `mode`/owner（owner 取调用进程身份）。新增 `Status` 取值：`ReadOnlyFs`(-EROFS)、`NoSpace`(-ENOSPC)、`AccessDenied`(-EACCES)、`IsDirectory`/`NotSeekable`(-ESPIPE，用于管道 lseek) 等，保持单一来源映射到 [errno.h](include/bigos/errno.h)。

- 理由：最小扩展现有 `File`/`FileOperations`，复用 offset/ref_count/close_on_exec；pipe 端也实现为带特殊 `FileOperations` 的 `File`（read/write 走环形缓冲、lseek 返回 `-ESPIPE`）。
- `write` 语义：从 `File.offset` 写入、推进 offset，越界/无空间/权限失败不推进 offset；`O_APPEND`（如支持）写前定位到末尾。`lseek` 校验 offset 溢出，管道/不可定位文件返回 `-ESPIPE`。
- 失败行为：与决策 3 一致映射；写失败前不推进 offset，确定性返回。

### 决策 5：pipe 为有界内核环形缓冲，两端各一 `File`，阻塞复用 wait queue，EOF/EPIPE 确定

`SYS_PIPE(out_fds[2])`：分配一个内核 `Pipe { uint8_t buf[N]; head/tail; read_open/write_open 引用计数; WaitQueue read_wq, write_wq; }`，创建读端 `File`（readable，FileOperations.read 从环形缓冲取、空且写端仍开 -> 在 `read_wq` 阻塞，空且写端全关 -> 返回 0 即 EOF）与写端 `File`（writable，write 满且读端仍开 -> 在 `write_wq` 阻塞，读端全关 -> 返回 `-EPIPE`，可选向写者投递 `SIGPIPE`），分配两个 fd 写回用户 `out_fds`。`dup`/`dup2` 复制 fd 指向同一 `File`（共享 offset 与端引用计数）。

- 理由：管道是 shell 管道的硬前置；环形缓冲 + 两端 `File` + wait queue 是标准最小实现，复用现有阻塞原语，零新调度机制。引用计数让「写端全关 -> 读 EOF、读端全关 -> 写 EPIPE」成为确定语义。
- 上下文：pipe read/write 的阻塞只在可阻塞进程上下文进行；IRQ/不可阻塞上下文调用确定性失败，绝不阻塞。
- `SIGPIPE`：本阶段默认以 `-EPIPE` 错误返回为主；若signal capability 信号已就位，可选在读端全关时向写者投递 `SIGPIPE`（默认 Terminate），由 design 固定为「默认返回 `-EPIPE`，`SIGPIPE` 投递为可选增强，二者语义在 spec 明确」。
- fork/exec：fork 子进程继承管道两端 fd 并增加端引用计数；exec 按 close-on-exec 关闭标记项，否则保留；exit/reap 关闭所有管道端 fd，端引用计数归零时回收 `Pipe`。
- 备选：(a) 无界缓冲 —— 破坏有界性，否决；(b) 用文件做管道 —— 语义错、需落盘，否决。

### 决策 6：syscall 号紧随现有末尾追加，扩展 open/write 语义不改号位

现有最大号是 `SYS_SIGRETURN = 19`。新增（最终集合在 specs 固化，此处为设计基线）：

```
SYS_LSEEK  = 20   // (fd, offset, whence) -> 新 offset 或 -EBADF/-ESPIPE/-EINVAL
SYS_PIPE   = 21   // (int out_fds[2]) -> 0 或 -EMFILE/-ENOMEM/-EFAULT
SYS_DUP    = 22   // (oldfd) -> newfd 或 -EBADF/-EMFILE
SYS_DUP2   = 23   // (oldfd, newfd) -> newfd 或 -EBADF/-EMFILE
SYS_FSYNC  = 24   // (fd) -> 0 或 -EBADF/-EIO
SYS_MKDIR  = 25   // (path, mode) -> 0 或 -EEXIST/-EACCES/-ENOSPC/-EROFS/-EINVAL
SYS_UNLINK = 26   // (path) -> 0 或 -ENOENT/-EACCES/-EISDIR/-EROFS/-EINVAL
```

`SYS_OPEN = 5` 扩展：接受 `O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC` flags 与 `O_CREAT` 的 `mode`（参数槽用现有寄存器约定，不改号位）；`SYS_WRITE = 2` 扩展：fd 指向文件/管道时写入对应 `File`（不再仅控制台 fd 1/2）。

- ABI：沿用现有寄存器约定（号 -> rax，参数 -> rdi/rsi/rdx/r10/r8/r9，返回值 -> rax），不发 EOI。只追加号、不改既有号位与寄存器布局，满足非破坏约束。写/管道/FS syscall 在分配或进入同步块 IO/阻塞前 MUST 检查调度阻塞守卫。
- 备选：把写相关合并成一个带子命令的 syscall —— 偏离现有一号一义风格，否决。

### 决策 7：`cred::may_access` 接成可写 FS 的实际强制点，确定性 `-EACCES`/`-EROFS`

可写 FS 的 open（写/创建）、`write`、`mkdir`、`unlink` 在执行前调用 `cred::may_access(file_uid, file_gid, mode, caller_uid, caller_gid, access_type)`：root 全放行；否则按 owner/group/other 与访问类型判定。拒绝 -> `-EACCES`；只读后端的写请求 -> `-EROFS`（先于或独立于权限判定，按 spec 文档化顺序）。判定逻辑零改动，仅新增接线点。

- 理由：time and identity capability 已实现并测试 `may_access` 纯判定，本阶段只新增其唯一/主要接线点，满足「升级强制点、不改语义」。`O_CREAT` 新建文件的 owner 取调用进程 uid/gid、mode 取调用方传入（经 umask 简化或直接采用，spec 明确）。
- 备选：FS 自带权限逻辑 —— 与 cred 重复，否决。

### 决策 8：写一致性语义固化为「write 经缓存可见、fsync/卸载前不保证落盘」

`write` 写入 buffer cache 并标脏后即对后续 `read`（同一或不同 fd）可见（页缓存语义）；落盘发生在 `fsync(fd)`、缓存淘汰回写、或显式 `sync_all()`。本阶段不保证 `write` 即落盘，明确记录为「write-back，落盘点为 fsync/淘汰回写」。

- 理由：这是标准页缓存语义且让「写后读回一致」与「有界落盘」解耦；smoke 用 `fsync` + 强制淘汰验证落盘后仍可读回。
- 失败行为：fsync 时块 IO 失败 -> `-EIO`，脏块保持 dirty 不丢数据；不 panic。
- 备选：write-through（每写即落盘）—— 正确但慢且每写打盘，本阶段以 write-back + fsync 为默认，write-through 留作可选。

### 决策 9：可写 FS 默认承载介质固化为 RAM-backed BlockDevice，磁盘分区承载留作后续可选

本阶段可写 FS（`bigfs`）的默认承载介质固化为**内存盘（RAM-backed BlockDevice）**：在内核内存中划出一段有界区域，包装成实现 `read_impl`/`write_impl` 的 `BlockDevice`，`bigfs` 在其上挂载读写。磁盘上保留分区承载明确**不在本阶段范围**。

- 理由：RAM-backed 介质让块写路径、buffer cache 写回与可写 FS 全链路在**不改动现有磁盘镜像、MBR、分区表与 exFAT 只读发现契约**的前提下端到端跑通并验证，风险最低；磁盘承载需要在镜像里安全保留一块不与现有 boot/exFAT 布局冲突的区域，属于跨 boot/磁盘布局的独立改动，与本阶段「立可写语义地基」目标正交。
- 语义影响：RAM-backed 介质重启不持久（断电即失），本阶段只保证「写后读回一致」「fsync 落盘到该块设备后淘汰再读一致」等**缓存/FS 正确性**语义，不承诺跨重启持久化；这一限制在 spec 与文档中明确。块写路径与 buffer cache 接口对 RAM-backed 与磁盘 `BlockDevice` 一致，后续切换承载介质不需改 FS/缓存逻辑。
- 失败行为：RAM 区域分配失败 -> 可写 FS 初始化确定性失败、不发布可写挂载，只读 exFAT 路径不受影响、不 panic。
- 备选：(a) 磁盘保留分区承载 —— 需改磁盘镜像/MBR/布局、风险高且与本阶段正交，否决为本阶段方案（列为后续可选演进，需安全保留分区时单独评估）；(b) 直接复用 exFAT 分区做写 —— 破坏只读 exFAT 契约，否决。

### 决策 10：管道关闭语义固化为「默认返回 `-EPIPE`，`SIGPIPE` 投递为可选增强」

读端全部关闭后写端写入的语义固化为：**默认返回确定性 `-EPIPE`**。是否额外向写者投递 `SIGPIPE`（默认动作 Terminate）作为**可选增强**，仅在signal capability 信号能力已就位且显式启用时生效；本阶段正确性与验证只依赖 `-EPIPE` 返回，不依赖 `SIGPIPE`。

- 理由：`-EPIPE` 返回是管道关闭语义的最小且自洽的正确面，shell 管道（shell 与用户态组合能力）即便没有 `SIGPIPE` 也能据此处理 broken pipe；把 `SIGPIPE` 设为可选可避免本阶段对信号子系统形成硬依赖，保持「先把通用 I/O 正确性立住」的最小目标。POSIX 下 `SIGPIPE` 与 `-EPIPE` 本就并存（信号被忽略/阻塞时写返回 `-EPIPE`），故默认 `-EPIPE` 与标准不冲突。
- 语义固化：spec 以 `-EPIPE` 为 MUST、`SIGPIPE` 为 MAY；smoke 验证「读端全关 -> 写 `-EPIPE`」为必测路径，`SIGPIPE` 投递不作为本阶段必测项。
- 失败行为：写端在读端全关时返回 `-EPIPE`，不阻塞、不写入；若启用可选 `SIGPIPE`，复用signal capability 既有投递路径（IRQ-return 边界、默认 Terminate），不在管道热路径新增信号机制。
- 备选：(a) 本阶段强制投递 `SIGPIPE` —— 对信号子系统形成硬依赖且扩大热路径耦合，超最小目标，否决；(b) 读端全关时静默丢弃写入 —— 破坏 broken-pipe 可观测语义，否决。

### 控制流总览

```
写文件路径 (可阻塞进程上下文):
  user int 0x80 SYS_WRITE(fd, buf, len) -> dispatch 检查阻塞守卫
    -> File.ops.write(file, buf, len):
         校验用户 buf (VMA-backed) -> cred::may_access(写) ? 继续 : -EACCES
         定位 (设备, 块号) -> bcache::get(dev, blk) (未命中经 read_sectors 装入)
         拷入块数据 -> bcache::mark_dirty -> 推进 file.offset -> 返回写入字节
  SYS_FSYNC(fd) -> bcache::sync(该文件涉及的脏块) 经 block::write_sectors 落盘 -> -EIO?

读文件路径:
  SYS_READ -> File.ops.read -> bcache::get 命中/装入 -> 拷出 -> 推进 offset

缓存淘汰:
  bcache::get 无空闲块 -> 选 ref==0 干净块复用; 若须淘汰脏块 -> 先 sync 回写再复用;
  无可淘汰 -> nullptr -> 上层 -ENOMEM/-ENOSPC (不阻塞死等, 不在 IRQ 落盘)

pipe 路径 (跨进程):
  SYS_PIPE -> 建 Pipe + 读端/写端 File + 两 fd
  写端 write 满 & 读端开 -> 在 write_wq 阻塞; 读端 read 取数据后 wake write_wq
  读端 read 空 & 写端开 -> 在 read_wq 阻塞; 写端 write 后 wake read_wq
  写端全关 -> 读端 read 返回 0 (EOF); 读端全关 -> 写端 write 返回 -EPIPE (可选 SIGPIPE)
  dup/dup2 -> newfd 指向同一 File, 共享 offset 与端引用计数

权限强制:
  open(写/创建)/write/mkdir/unlink -> cred::may_access(owner/mode, caller, access) 拒绝 -> -EACCES
  只读后端写请求 -> -EROFS
```

## Risks / Trade-offs

- [buffer cache 落盘或装入在不可阻塞上下文被调用，导致死锁/破坏 IRQ 安全] → `get`(装入)/`sync`(落盘) 只在可阻塞进程上下文调用，syscall 层在分配/进入同步块 IO 前检查现有调度阻塞守卫；IRQ/不可阻塞上下文调用确定性失败，绝不落盘/阻塞。
- [块设备写破坏现有只读读路径或 ATA PIO 状态机] → `BlockDevice` 写入口为追加字段、`write_impl==nullptr` 视为只读；审查 ATA PIO BSY/DRDY/DRQ 时序确保写后读不受影响；写失败保留脏块不丢数据。
- [可写 FS 元数据（位图/inode/目录项）更新中途失败导致不一致] → 单核同步、无并发；失败路径在提交元数据前校验空间/合法性，失败时不写半成品元数据；本阶段不做 journaling（列为非目标），明确记录崩溃一致性限制与残余风险。
- [pipe 阻塞与现有 wait queue/blocking guard 交互引入唤醒丢失或重复回收] → 复用现有 wake-one/all 与端引用计数；读/写端 `File` 关闭时确定性 wake 对端并按引用计数回收 `Pipe`；exit/reap 关闭所有端，引用计数归零唯一回收。
- [dup/dup2 共享 `File` 的引用计数错误导致 use-after-free 或泄漏] → `dup`/`dup2` 增 `File.ref_count`，close 减一、归零释放；fork 继承增计数、exec close-on-exec 关闭、exit/reap 关闭全部，确保每个 fd 精确一次释放。
- [新增/扩展 syscall 改动 ABI 风险] → 只追加号位、不改既有号与寄存器布局；扩展 open/write 仅扩展语义不改号位；写/管道 syscall 不发 EOI、不放宽门 DPL。
- [可写 FS 后端选型若选 exFAT 写会高风险破坏只读契约] → 本阶段选最小自定义可写 FS（`bigfs`）+ RAM-backed 默认验证介质，不触碰现有 exFAT 只读路径与磁盘镜像/MBR 布局；exFAT 写留作后续可选。
- [runtime smoke 在本地 emulator/toolchain 不可用] → validation 必须记录缺失工具、替代 source/build checks、跳过原因与残余块 IO/缓存一致性/阻塞风险，不得声称已做运行时验证。

## Migration Plan

- 纯增量：新增 bcache 模块、`bigfs` 后端、pipe 模块、块写路径、VFS 写/lseek op、新增/扩展 syscall 分支、`cred::may_access` 接线；默认参与正常启动但不改变既有行为（无进程发起写/管道时，新路径不被触发，只读 exFAT 行为不变）。
- `BlockDevice` 写入口与 `FileOperations` 写/lseek op 为追加字段，不破坏现有只读读布局；只读后端写 op 返回 `-EROFS`。
- 回滚：移除验证开关、新增 syscall 分支、bcache/`bigfs`/pipe 模块与块写入口即可回到原状；追加字段不影响既有只读路径假设。
- 验证开关（`writable_fs_smoke` / `pipe_smoke`）默认关闭，不影响默认启动 marker 与既有 smoke 矩阵。

## Open Questions

- 无。原先的两个待定项已收敛为决策 9（可写 FS 默认承载介质固化为 RAM-backed BlockDevice，磁盘分区承载留作后续可选）与决策 10（管道关闭语义固化为「默认返回 `-EPIPE`，`SIGPIPE` 投递为可选增强」）。如后续需要跨重启持久化到磁盘分区，或shell 与用户态组合能力 shell 对 `SIGPIPE` 投递有更强期望，再在对应阶段单独评估，不影响本阶段最小交付。
