# 可写文件系统、页/块缓存与管道

BigOS 可写文件系统与 pipe/dup foundation 在现有只读 I/O 栈（只读 ATA PIO、只读 exFAT 挂载、只读 fd/VFS
壳层）之上加入第一层可写 I/O 地基：内核块缓冲缓存、块设备写路径、最小可写文件
系统（`bigfs`）以及 `pipe`/`dup`/`dup2`。owner/mode 权限纯原语
`cred::may_access`（time and identity capability）在可写路径上成为实际强制点。只读 exFAT 路径、磁盘
镜像、MBR/分区发现、`int 0x80` 寄存器 ABI、IDT/向量布局、DPL 设置、页表自映射与
CR3 约定均不变。

## 块缓冲缓存

`bigos::bcache`（`include/bigos/fs/bcache.h`、`kernel/core/fs/bcache.cc`）以
`(BlockDevice*, block_no)` 为键缓存固定大小块。块大小为一个扇区（512 字节），
容量为有界编译期常量（`CACHE_BLOCKS`）。缓存数据页用 `alloc_kernel_pages` 配合
`_GFM_PRE_PAGING` 一次性分配。

- `get(dev, block_no)` 返回引用计数加一的块；命中直接返回不再读盘，未命中经
  `block::read_sectors` 装入。
- `put`、`mark_dirty`、`sync`、`sync_all` 分别释放引用、标脏、经
  `block::write_sectors` 回写脏块。
- `sync_block(dev, block_no)` 允许文件系统按显式顺序回写选定的 dirty block。选定块
  未缓存或已经 clean 视为成功；写失败时对应缓存块保持 dirty 或 pending。
- 写回（write-back）语义：写入只标脏，落盘点为 `fsync`、淘汰回写或 `sync_all`。
- 淘汰优先选未被引用的干净块（不触发落盘）；唯一可复用块为脏块时先回写再复用。
  所有槽位都被引用时 `get` 返回 `nullptr`（上层映射为 `-ENOMEM`/`-ENOSPC`），绝
  不死等阻塞、绝不在 IRQ 上下文落盘。强制设备失效遇到 dirty block 写失败时也会保
  留 dirty 缓存块，而不是把槽位当作已 durable 后丢弃。
- 装入与回写执行同步块 IO，MUST 只在可阻塞进程上下文运行；syscall 层在进入前检查
  调度阻塞守卫。设备写失败时块保持 dirty（不丢数据）并返回 `IoError`。

## 块设备写路径

`BlockDevice` 追加 `write_impl` 字段（`write_impl` 为空表示只读设备）并新增
`block::write_sectors(dev, lba, count, src, src_len)`，与 `read_sectors` 对称。
发起设备写前校验扇区数、源缓冲长度与 LBA 范围溢出。ATA PIO 后端实现 LBA48
WRITE SECTORS EXT 加 FLUSH CACHE EXT，复用现有 BSY/DRDY/DRQ 轮询时序。只读设备返
回 `Unsupported`，上层映射为 `-EROFS`。

## 可写文件系统（`bigfs`）

`bigos::bigfs`（`include/bigos/fs/bigfs.h`、`kernel/core/fs/bigfs.cc`）是挂载在
`/rw` 的最小可写文件系统，与只读 exFAT 挂载并存。默认承载介质为 RAM-backed
`BlockDevice`（决策 9），整条写路径因此可端到端跑通而不触碰磁盘镜像。布局（以
512 字节块计）：超级块、inode 位图、数据块位图、inode 表、数据区。全程有界：固定
inode 数、仅直接块的文件（有界 `MAX_FILE_SIZE`）、定长目录项。所有元数据与数据均
经块缓冲缓存读写。

每个 inode 携带 `owner`（uid/gid）与 `mode`。支持：可写 `open`
（`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`）、文件 `write`、`lseek`、`O_TRUNC`
截断、`mkdir`、最小目录枚举、`unlink` 与可写 backend 内受限的 regular-file
`rename`。`unlink` 会先移除目录项；仍打开的 fd 会让 inode 与数据块保留到最后一个
引用关闭，rename 前已打开的 fd 在成功 rename 后仍指向同一个运行期文件。失败语义确定性
（`-ENOSPC`/`-EEXIST`/`-ENOENT`/`-ENOTDIR`/`-EISDIR`/`-EINVAL`/`-ENOTEMPTY`/`-EIO`/
`-EACCES`/`-EROFS`），失败路径绝不发布半成品元数据。

默认介质为 RAM-backed，重启不持久；该模式只保证当前运行期一致性（写后读回、
metadata 与目录枚举可见性，以及 `fsync` 加缓存淘汰后再读一致）。

`BIGOS_PERSISTENT_WRITABLE_FS` 会选择独立测试磁盘上的可选 persistent `/rw`
backend。持久布局复用同一组有界 BigFS 限制，并增加显式 superblock
magic/version/block-size/capacity/root metadata checksum。Normal boot 只在识别既有
兼容卷且有界 metadata validation 成功后挂载；invalid magic、unsupported version、非法
容量、inode 或 directory-entry 越界、block mapping 冲突、data bitmap 所有权矛盾会降
级到 RAM-backed `/rw`，不会自动格式化、repair 或 migrate。持久 metadata 更新使用有
界 ordered commit unit 回写选定 cache blocks：新初始化的数据块或目录块会先于发布它
们的 inode/目录 metadata 同步；释放块前会先同步 inode 引用移除，再把块记录为可持久
复用。受限 `/bin/mkfs_bigfs` 工具调用 BigOS 专用的显式格式化 hook，只面向配置好的
persistent test disk；它不是 POSIX `mkfs`、`mount` 或设备管理工具。Persistent 模式
只承诺成功 `fsync`/write-back 后经 clean reboot 可见。metadata 写回失败返回确定性错
误并保留 dirty/pending cache state，不报告 durable success。不提供 journaling、crash
recovery、async I/O、广泛存储驱动、stable inode identity、完整 POSIX `DIR*` 或掉电一
致性保证。

## 权限强制

`bigfs` 的 open（写/创建）、`write`、`mkdir`、`unlink` 在修改状态前调用
`cred::permits(file_uid, file_gid, mode, req_uid, req_gid, access)`：root 全放行，
否则按 owner/group/other 位判定。拒绝返回 `-EACCES` 且不修改文件系统状态。新文件
owner 取调用进程身份、mode 取调用方传入值。只读 exFAT 后端对任何写/创建请求返回
`-EROFS`。判定逻辑本身相对time and identity capability 不变。

## VFS 与 fd 扩展

`FileOperations` 追加 `write`、`lseek`、`truncate` 与最小 `readdir` op，`File`
追加 `writable` 标志（保留只读 `read`/`close` 布局）。`write` op 为空的后端即只读
（`write` 返回 `-EROFS`）；`lseek` op 为空时使用带溢出检查的普通 offset 运算。
`open_absolute` 增加可写重载，接受创建 flags 与 `O_CREAT` 的 mode/owner；以只读方式
打开 `/rw` 目录会得到可枚举有界 name/type 记录的目录 fd。fd 层新增 `dup`/`dup2`
（共享同一 `File` 与 offset，每个新 fd 增引用一次；`dup2` 先关闭已打开的目标），
以及经进程局部 fd 的 `write`/`lseek`/`fsync`/有界 `ftruncate`/最小目录枚举。
libc 的 `truncate(path, len)` wrapper 由有界 open + `ftruncate` 组合实现，不表示完整
POSIX 路径 API。

`/rw` 常规文件的扩展写可以在 `MAX_FILE_SIZE` 内追加、跨 cache block 或 seek 越过
EOF。新暴露 gap 在被覆盖前读取为零。收缩 truncate 会先发布新 size，再让尾部块回到
free set；扩展 truncate 暴露 zero-read 范围，但不承诺完整 sparse-file 或 hole
preservation API。复用块在用户可读前必须被清零或经完整 staging 覆盖。

## 管道

`bigos::ipc`（`include/bigos/ipc/pipe.h`、`kernel/core/ipc/pipe.cc`）提供有界环形
缓冲管道与一对相连的读端/写端 `File`。缓冲空且写端开时读阻塞、写入后唤醒；缓冲满
且读端开时写阻塞、读出后唤醒。阻塞只在可阻塞进程上下文进行，不可阻塞上下文确定性
失败。写端全关后读返回 0（EOF）；读端全关后写返回 `-EPIPE`（`SIGPIPE` 投递为可选
增强，决策 10）。对管道 `lseek` 返回 `-ESPIPE`。端引用计数精确管理：`fork` 继承端
并增计数，`exec` 遵守 close-on-exec，exit/reap 关闭每个剩余端各一次，两端归零时回
收管道对象。

## syscall ABI

新号紧随 `SYS_SIGRETURN = 19` 追加：`SYS_LSEEK = 20`、`SYS_PIPE = 21`、
`SYS_DUP = 22`、`SYS_DUP2 = 23`、`SYS_FSYNC = 24`、`SYS_MKDIR = 25`、
`SYS_UNLINK = 26`、`SYS_EXECVE = 27`、`SYS_READDIR = 28`、`SYS_RMDIR = 38`、
`SYS_FTRUNCATE = 39`。`SYS_OPEN = 5`
扩展为接受可写/创建 flags 与 `O_CREAT` 的 mode；`SYS_WRITE = 2` 扩展为写文件/管道
fd，同时保留控制台快路径。寄存器 ABI、既有号位、向量布局与「syscall 不发 EOI」规则
不变。写/管道/FS syscall 在分配或进入同步块 IO/阻塞前检查调度阻塞守卫。

## 验证 smoke

两个默认关闭开关控制运行时 smoke；既有 smoke 矩阵不变。

- `xmake f --writable_fs_smoke=y` 启用 `BIGOS_WRITABLE_FS_SMOKE`。在可阻塞内核线
  程中覆盖 `O_CREAT` 建文件 + 写 + 读回、append/cross-block/seek-past-EOF 增长、
  zero gap 读取、收缩/扩展 truncate、块复用清零、容量失败、`fsync` 后强制淘汰再读
  一致、owner/mode 权限拒绝、只读后端写被 `-EROFS` 拒绝，发射
  `BIGOS_WRITABLE_FS_PASSED`/`BIGOS_WRITABLE_FS_FAILED`。
- `xmake f --pipe_smoke=y` 启用 `BIGOS_PIPE_SMOKE`，覆盖跨线程 FIFO 写读、读空阻
  塞 + 写入唤醒、写端全关读 EOF、读端全关写 `-EPIPE`，发射
  `BIGOS_PIPE_PASSED`/`BIGOS_PIPE_FAILED`。
- `xmake f --persistent_writable_fs_smoke=y` 启用
  `BIGOS_PERSISTENT_WRITABLE_FS_SMOKE` 和 persistent backend。helper 可通过
  `--persistent-image` 挂载独立测试磁盘；第一次 boot 格式化/写入/`fsync` metadata
  consistency state 并等待 `BIGOS_PERSISTENT_WRITABLE_FS_WRITE_PASSED`，第二次 boot
  复用同一 persistent image 并等待 `BIGOS_PERSISTENT_WRITABLE_FS_VERIFY_PASSED`。
  检查状态覆盖目录项、inode metadata、file size、block mapping、free-space bitmap
  effects、stable growth/truncate 行为以及只读 exFAT 隔离。

无界 QEMU 串口 marker smoke 示例：

```bash
xmake f --writable_fs_smoke=y
uv run python tools/boot_debug.py run --emulator qemu --display none \
  --serial-log build/test/serial.log --expect-serial-marker BIGOS_WRITABLE_FS_PASSED
```

当 QEMU/Bochs、ROM/显示、交叉工具链或磁盘镜像不可用时，记录缺失工具、跳过的验证
与残留风险，不得声称已做运行时验证。

## 非目标

- 无硬/软链接、广泛跨 backend 或目录 `rename`、完整 `stat`/`fstat`、完整 `fcntl`，
  或完整 `readdir`/`getdents` 遍历。
- 无 file-backed mmap 与页缓存共享映射、多挂载命名空间、`mount`/`umount`、
  journaling、`fsck`、配额、ACL/xattr。
- 无命名管道（FIFO）/`mknod`/socket，除可选 `SIGPIPE` 外无其他管道信号语义。
- persistent `/rw` 不提供 journaling、crash recovery、async I/O、广泛存储驱动、通用
  block-device 管理或完整 POSIX filesystem 兼容性。
- 无 SMP 缓存一致性与写性能优化（仅保证正确性与有界性）。
- 不改 `int 0x80` 寄存器 ABI、既有 syscall 号、IDT/向量布局、DPL、页表/CR3/地址
  布局、外部 IRQ/异常 EOI 语义，以及 MBR/分区/exFAT 只读发现与磁盘镜像布局。
