# 文件描述符与 VFS 壳层

BigOS 阶段 13 引入最小的只读 fd/VFS 边界。后续阶段保持这条 exFAT 读路径不变，并在其上加入 bounded VMA metadata、`brk`、restricted anonymous mapping、demand paging、可写 `/rw`、page/buffer cache、pipe/dup 和最小用户态运行时。

## VFS 边界

- `include/bigos/fs/vfs.h` 定义公开壳层：`Vnode`、`File`、
  `FileOperations`、只读 open flags、确定性 status codes，以及
  `init`/`open_absolute`/`read`/`release`。
- `kernel/core/fs/vfs.cc` 持有单 root mount。`vfs::init()` 初始化 ATA PIO
  primary-master block device，发现现有 MBR exFAT 分区，只读挂载，并且只在挂载成功后发布 root。
- exFAT backend 是 `find_exfat_partition`、`mount_exfat`、`lookup` 和
  `read_file` 的 adapter；不重写 parser，也不改变 on-disk 支持范围。
- `open_absolute` 只接受有界 NUL 结尾的绝对路径和 read-only flags。相对路径、`.`/`..`
  component、目录、缺失文件以及 write/create/truncate flags 都确定性失败。
- `read` 使用 open file offset，在 EOF 处 clamp，只在 backend read 成功后推进 offset，并拒绝 offset 算术溢出。

## 进程 fd table

- 每个 `Process` 持有可增长的 descriptor table（堆分配的 `FdEntry` 数组，由 `MAX_FDS_SOFT_LIMIT` 软上限约束，而非固定内联大小），entry 指向 VFS `File` 对象；存储按需惰性分配，并在进程被 reap 时释放。
- `open` 在当前进程 table 中安装最低可用 fd，并按需增长 table；达到 fd 软上限或增长分配失败时返回确定性的 `-bigos::EMFILE`，并 drop 未发布的 file reference。
- `read` 和 `close` 对越界、未使用、已关闭以及不可读 descriptor 返回确定性 bad-fd 错误。
- `exec` 保留未设置内部 `close_on_exec` bit 的 descriptor；rollback 不破坏旧 fd table。
- exit 与 fault path 将 fd-backed 对象销毁延后到 `reap_pending_processes()`，并且需要先通过 active-stack 和 active-CR3 检查。

## Syscall ABI

- `SYS_OPEN = 5`：`rdi=path`，`rsi=flags`，返回 process-local fd 或确定性负错误码。
- `SYS_READ = 6`：`rdi=fd`，`rsi=user_buffer`，`rdx=len`，通过有界 kernel buffer copy，返回 byte count 或负错误码。
- `SYS_CLOSE = 7`：`rdi=fd`，移除 descriptor 并 drop file reference。
- `SYS_STAT = 29`：`rdi=path`，`rsi=struct stat*`，返回绝对路径的 BigOS 有界 metadata snapshot。
- `SYS_FSTAT = 30`：`rdi=fd`，`rsi=struct stat*`，返回 open file object 的 metadata，且不推进 offset。
- fd/VFS syscall 在初始化 VFS、分配 file 对象或进入同步 exFAT/ATA PIO read 前检查 `sched::can_block()`。
- `int 0x80` vector 和寄存器 ABI 不变。syscall gate 使用 DPL=3 trap gate，使普通进程 syscall 保留 IF 并通过 blocking guard；CPU exception 与外部 IRQ 仍是 nonblocking context，EOI 规则不变。

## 验证

- Source-level checks 覆盖 VFS root 发布、open 拒绝、fd 容量、bad fd 与 double-close、EOF clamp、offset advancement、metadata snapshot、exec close-on-exec，以及 safe reaper close-all。
- 现有 `fs_smoke` case 现在通过 VFS open/read/release 验证 `/boot/fs_smoke.txt`，并输出既有 `BIGOS_FS_EXFAT_READ_PASSED` marker。
- `user_elf_smoke` 通过 VFS 读取 `/boot/user/init.elf`，再把有界 image 交给既有 ELF process loader。

## 有界元数据

- BigOS 通过 `stat`/`fstat` wrapper 和打包的 `/bin/stat` 观察工具暴露小型 metadata 结构。字段包含对象类型、大小、mode、uid、gid、有界 link-count 默认值、第一版始终为零的用户可见对象编号，以及显式零填充保留字段。
- exFAT metadata 是只读的，并为 owner/mode 返回文档化默认值。`/rw` metadata 反映运行期 create、write、truncate、mkdir、unlink 和权限 metadata 变化，但仍是 RAM-backed，且不承诺重启后持久化。
- 这是 BigOS bounded metadata subset，不是完整 POSIX `struct stat`、设备节点、符号链接、ACL、xattr、完整时间戳、稳定 inode 或持久对象身份语义。

## 非目标

- 当前基线仍不引入 cwd、相对路径解析、`select`、完整 POSIX `stat`、demand paging、COW、SMP 或 UEFI backend；其中一部分是后续 bounded 能力，另一部分仍是非目标。
- 当前项目非目标仍包括 async I/O、广泛或 file-backed `mmap`、完整 POSIX 文件系统/进程语义、动态链接、SMP 和可运行 UEFI backend。
