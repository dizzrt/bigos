# 文件描述符与 VFS 壳层

BigOS 引入最小的只读 fd/VFS 边界。后续能力保持这条 exFAT 读路径不变，并在其上加入 bounded VMA metadata、`brk`、restricted anonymous mapping、demand paging、可写 `/rw`、page/buffer cache、pipe/dup 和最小用户态运行时。

## VFS 边界

- `include/bigos/fs/vfs.h` 定义公开壳层：`Vnode`、`File`、
  `FileOperations`、open flags、确定性 status codes、cwd-aware path resolution，
  以及 `init`/`open`/`open_absolute`/`read`/`release`。
- `kernel/core/fs/vfs.cc` 持有单 root mount。`vfs::init()` 初始化 ATA PIO
  primary-master block device，发现现有 MBR exFAT 分区，只读挂载，并且只在挂载成功后发布 root。
- exFAT backend 是 `find_exfat_partition`、`mount_exfat`、`lookup` 和
  `read_file` 的 adapter；不重写 parser，也不改变 on-disk 支持范围。
- Path-taking VFS 入口共享一个有界 resolver。绝对路径从 root 解析；相对路径从当前进程 cwd
  解析；重复 separator 会被折叠；POSIX-style `.` 和 `..` component 会被归约；root 的父目录仍是 root。
- 该 resolver 不实现 symlink traversal、mount namespace、`chroot` 或完整 POSIX `realpath`
  canonicalization。空路径、过长路径或不支持的路径形式会在发布 fd 或修改文件系统状态前确定性失败。
- `read` 使用 open file offset，在 EOF 处 clamp，只在 backend read 成功后推进 offset，并拒绝 offset 算术溢出。

## 进程 fd table

- 每个 `Process` 持有可增长的 descriptor table（堆分配的 `FdEntry` 数组，由 `MAX_FDS_SOFT_LIMIT` 软上限约束，而非固定内联大小），entry 指向 VFS `File` 对象；存储按需惰性分配，并在进程被 reap 时释放。
- `open` 在当前进程 table 中安装最低可用 fd，并按需增长 table；达到 fd 软上限或增长分配失败时返回确定性的 `-bigos::EMFILE`，并 drop 未发布的 file reference。
- `read` 和 `close` 对越界、未使用、已关闭以及不可读 descriptor 返回确定性 bad-fd 错误。
- 有界 fd-control 暴露每个 descriptor entry 的 close-on-exec 状态和 `F_DUPFD`。
  `F_GETFD` 只观察 entry flag，`F_SETFD` 只接受 `FD_CLOEXEC`，`F_DUPFD` 在不小于
  调用方最小值的位置分配最低可用 fd，复用同一个 open file object，并清除新
  descriptor 的 close-on-exec。
- `exec` 保留未设置内部 `close_on_exec` bit 的 descriptor；rollback 不破坏旧 fd table。
- 每个用户 `Process` 还拥有一个内联有界 cwd 字符串。它初始化为 `/`，由 `fork`
  独立复制，被 `execve` 保留，并且 safe reap 时不需要 cwd heap teardown。
- exit 与 fault path 将 fd-backed 对象销毁延后到 `reap_pending_processes()`，并且需要先通过 active-stack 和 active-CR3 检查。

## Syscall ABI

- `SYS_OPEN = 5`：`rdi=path`，`rsi=flags`，返回 process-local fd 或确定性负错误码。
- `SYS_READ = 6`：`rdi=fd`，`rsi=user_buffer`，`rdx=len`，通过有界 kernel buffer copy，返回 byte count 或负错误码。
- `SYS_CLOSE = 7`：`rdi=fd`，移除 descriptor 并 drop file reference。
- `SYS_STAT = 29`：`rdi=path`，`rsi=struct stat*`，返回绝对路径的 BigOS 有界 metadata snapshot。
- `SYS_FSTAT = 30`：`rdi=fd`，`rsi=struct stat*`，返回 open file object 的 metadata，且不推进 offset。
- `SYS_FTRUNCATE = 39`：`rdi=fd`，`rsi=length`，对可写 `/rw` 常规文件执行有界
  truncate。收缩保留前缀并在发布新 size 后释放尾部块；扩展暴露 zero-read 字节。它
  不是完整 POSIX `ftruncate(2)`。
- `SYS_FCNTL = 48`：`rdi=fd`，`rsi=cmd`，`rdx=arg`，只支持
  `F_GETFD`/`F_SETFD`/`F_DUPFD`。
- `SYS_ACCESS = 49`：`rdi=path`，`rsi=mode`，使用共享 cwd-aware path resolver 与
  有界 metadata/access 模型，不发布 fd。
- `SYS_TRUNCATE = 50`：`rdi=path`，`rsi=length`，对可写 `/rw` regular file 执行
  有界 path truncate。
- `SYS_CHDIR = 31`：`rdi=path`，解析目标，确认它是目录，并且只在成功时提交新 cwd。
- `SYS_GETCWD = 32`：`rdi=user_buffer`，`rsi=len`，复制 NUL 结尾 cwd 字符串；有效缓冲区过小时返回 `-ERANGE`。
- fd/VFS syscall 在初始化 VFS、分配 file 对象或进入同步 exFAT/ATA PIO read 前检查 `sched::can_block()`。
- `int 0x80` vector 和寄存器 ABI 不变。syscall gate 使用 DPL=3 trap gate，使普通进程 syscall 保留 IF 并通过 blocking guard；CPU exception 与外部 IRQ 仍是 nonblocking context，EOI 规则不变。

## 验证

- Source-level checks 覆盖 VFS root 发布、cwd resolution、open 拒绝、fd 容量、bad fd 与 double-close、EOF clamp、offset advancement、metadata snapshot、fd-control close-on-exec/F_DUPFD 行为、access/truncate syscall routing、exec close-on-exec，以及 safe reaper close-all。
- 现有 `fs_smoke` case 现在通过 VFS open/read/release 验证 `/boot/fs_smoke.txt`，并输出既有 `BIGOS_FS_EXFAT_READ_PASSED` marker。
- `user_elf_smoke` 通过 VFS 读取 `/boot/user/init.elf`，再把有界 image 交给既有 ELF process loader。

## 有界元数据

- BigOS 通过 `stat`/`fstat` wrapper 和打包的 `/bin/stat` 观察工具暴露小型 metadata 结构。字段包含对象类型、大小、mode、uid、gid、有界 link-count 默认值、第一版始终为零的用户可见对象编号，以及显式零填充保留字段。
- exFAT metadata 是只读的，并为 owner/mode 返回文档化默认值。`/rw` metadata 反映运行期 create、write、truncate、mkdir、unlink 和权限 metadata 变化。RAM-backed `/rw` 仍不跨重启持久化；persistent 测试后端只声明成功 `fsync` 与 clean reboot 后的 clean-sync 状态。
- 这是 BigOS bounded metadata subset，不是完整 POSIX `struct stat`、设备节点、符号链接、ACL、xattr、完整时间戳、稳定 inode 或持久对象身份语义。

## 非目标

- 当前基线仍不引入 `select`、完整 POSIX `stat`、完整 pathname canonicalization、symlink traversal、mount namespace 或 `chroot`；其中一部分是后续 bounded 能力，另一部分仍是非目标。
- 当前项目非目标仍包括广泛 user-visible async I/O、广泛 writable file-backed `mmap`、完整 POSIX 文件系统/进程语义、完整 POSIX dynamic-loader 语义、CPU hotplug、NUMA 和广泛 storage/device management。
