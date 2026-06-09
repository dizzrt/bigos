# 文件描述符与 VFS 壳层

BigOS 阶段 13 在可写文件系统、page cache、广泛 `mmap` 或用户态 libc 之前，引入最小的只读 fd/VFS 边界。阶段 14 在该边界之上加入 bounded VMA metadata、`brk`、restricted anonymous mapping 和 VMA-backed syscall-buffer validation。

## VFS 边界

- `include/bigos/fs/vfs.h` 定义公开壳层：`Vnode`、`File`、
  `FileOperations`、只读 open flags、确定性 status codes，以及
  `init`/`open_absolute`/`read`/`release`。
- `src/kernel/fs/vfs.cc` 持有单 root mount。`vfs::init()` 初始化 ATA PIO
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
- fd/VFS syscall 在初始化 VFS、分配 file 对象或进入同步 exFAT/ATA PIO read 前检查 `sched::can_block()`。
- `int 0x80` vector 和寄存器 ABI 不变。syscall gate 使用 DPL=3 trap gate，使普通进程 syscall 保留 IF 并通过 blocking guard；CPU exception 与外部 IRQ 仍是 nonblocking context，EOI 规则不变。

## 验证

- Source-level checks 覆盖 VFS root 发布、open 拒绝、fd 容量、bad fd 与 double-close、EOF clamp、offset advancement、exec close-on-exec，以及 safe reaper close-all。
- 现有 `fs_smoke` case 现在通过 VFS open/read/release 验证 `/boot/fs_smoke.txt`，并输出既有 `BIGOS_FS_EXFAT_READ_PASSED` marker。
- `user_elf_smoke` 通过 VFS 读取 `/boot/user/init.elf`，再把有界 image 交给既有 ELF process loader。

## 非目标

- fd/VFS 壳层不引入 regular file write syscall、目录变更、权限、cwd、相对路径解析、`dup`、`pipe`、`select`、`lseek`、`stat`、page cache、async I/O、广泛或 file-backed `mmap`、demand paging、COW、用户态 libc、SMP 或 UEFI backend。`brk` 与 restricted anonymous mapping 由后续 bounded VMA/user-memory API 覆盖。
