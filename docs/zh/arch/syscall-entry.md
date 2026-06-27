# 系统调用入口（syscall entry）

BigOS user entry and syscall capability 使用一条受控的“软件主动进入内核”路径与最小 syscall ABI。ring0 syscall diagnostic capability 的 ring0 诊断 syscall 仍保留；默认关闭的 `user_program_smoke` 路径会在配置 GDT/TSS 与用户地址空间后允许 CPL3 通过 `int 0x80` 进入同一个 dispatcher。

## 入口机制选择：`int 0x80` 软件中断门

本阶段采用 `int 0x80` 软件 gate，而非 `syscall`/`sysret` 快速系统调用：

- 复用既有 kernel-owned 静态 IDT + `interrupt.s` 的 `isr_common` + `irq_dispatch` 框架。vector `0x80` 的 `isr_entry` stub 与 dispatch 框架已存在，因此几乎是“零新增汇编”的入口：只需在 `irq_dispatch` 中识别 syscall vector 并路由到 syscall dispatcher。
- 取舍：`syscall`/`sysret` 需要配置 `IA32_STAR/LSTAR/FMASK` MSR、定义内核/用户段排列约束、并准备 `swapgs`/内核栈策略；当前 change 继续使用更可解释的 IDT gate + TSS/RSP0。
- DPL：仅 `VECTOR_SYSCALL` gate 配置为 DPL=3。它是 trap gate，使普通进程 syscall 保留 IF，fd/VFS syscall 可以通过 `sched::can_block()`。其它 CPU exception 与 i8259 IRQ gate 保持 ring0-only interrupt gate。syscall 仍不是外部 IRQ，dispatch 路径不发送 i8259 EOI。
- vector 用具名常量 `VECTOR_SYSCALL = 0x80` 固定，集中声明在 `include/irq/interrupt.h`，避免散落魔数。

## 最小 syscall ABI

syscall number、参数、返回值与 `InterruptFrame` 字段的对应关系（声明在 `include/bigos/syscall.h`，并由源码级检查断言）：

| 角色          | 寄存器 | `InterruptFrame` 字段 |
| ------------- | ------ | --------------------- |
| syscall number | `rax`  | `InterruptFrame.rax`  |
| 参数 0        | `rdi`  | `InterruptFrame.rdi`  |
| 参数 1        | `rsi`  | `InterruptFrame.rsi`  |
| 参数 2        | `rdx`  | `InterruptFrame.rdx`  |
| 参数 3        | `r10`  | `InterruptFrame.r10`  |
| 参数 4        | `r8`   | `InterruptFrame.r8`   |
| 参数 5        | `r9`   | `InterruptFrame.r9`   |
| 返回值        | `rax`  | dispatcher 写回 `InterruptFrame.rax` |

- syscall number 通过 `rax` 传入；返回值通过 `rax` 写回，即 dispatcher 写 `InterruptFrame.rax`，调用方在 `iretq` 返回后从 `rax` 读取结果。
- 第 4 个参数使用 `r10`（而非 `rcx`），贴近 SysV/Linux x86_64 syscall 约定，并避免与 `int 0x80` / `iretq` 下被破坏的 `rcx` 语义冲突。
- 除返回值外的寄存器约定为 callee 可 clobber，调用方负责保存。
- ABI 与具体入口机制解耦：dispatcher 以 `InterruptFrame` 为输入；未来若换 `syscall`/`sysret` 只需替换入口 stub，ABI 与 dispatch 可复用。

## dispatch 与未知 number 处理

`bigos::sys::dispatch(InterruptFrame*)`：

- 从 `InterruptFrame.rax` 读取 number，用 bounded switch 路由到内核实现。
- 已知 number 调用对应实现，返回值经 `rax` 写回。
- 未知 number 在 `rax` 写入确定性负错误码 `-bigos::ENOSYS`（数值 `-38`），不崩溃、不进入 CPU 异常路径。POSIX 风格错误码统一以正值集中定义于 `include/bigos/errno.h`，写入返回寄存器时取负。
- 在 `irq_dispatch` 中通过 `is_syscall_vector(vector == VECTOR_SYSCALL)` 识别 syscall，命中后调用 `bigos::sys::dispatch` 并直接返回。该路径 **MUST NOT** 发送 i8259 EOI（syscall 不是外部 IRQ）；CPU 异常、外部 IRQ、syscall 三类入口的 EOI 语义保持分离不变。

## 诊断型 syscall

- `SYS_DEBUG_WRITE`（number=0）：把内核内固定/受限 buffer 经现有 serial/console 输出确定性 marker `BIGOS_SYSCALL_WRITE`，并返回写出的字节数。本阶段调用方为内核态，buffer 为内核内 bounded 来源；**不做用户指针校验**。
  - **ring3 前置项**：引入 ring3 后，用户态传入的 buffer 指针与长度 **必须** 做用户地址空间范围校验与 bounded 拷贝后才能输出。
- `SYS_GET_TICK`（number=1）：返回 `bigos::timer::ticks()` 单调 tick，验证返回值寄存器路径。`timer::ticks()` 已通过 `include/bigos/timer.h` 稳定暴露，是 context-agnostic bounded read，故选用它而非 `SYS_DEBUG_NOOP`。
- `SYS_WRITE`（number=2）：仅支持早期 console sink（当前固定 `fd=1`），在读取用户 buffer 前检查低半区范围、页表 present/user bit 和最大长度 `SYS_WRITE_MAX_LEN`，再把 bounded 内容输出到 serial/VGA，并返回确定性字节数或 `-bigos::EFAULT`。
- `SYS_EXIT`（number=3）：记录当前用户进程 exit code，标记 terminated，恢复内核地址空间并转入 scheduler 的延后回收退出路径；该 syscall 不返回到已终止用户指令流。
- `SYS_WAIT`（number=4）：保留旧的二参 raw wait 形态，支持 `WAIT_ANY` 或指定 child pid，并可选地把有界 raw exit status 拷贝到用户 `int*`。
- `SYS_OPEN`（number=5）：复制有界 NUL 结尾用户 path，接受 VFS 已实现的有界 open flags（`O_RDONLY`/`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`），经 VFS 壳层 open，并返回 process-local fd。
- `SYS_READ`（number=6）：验证用户目标 range，经进程 fd table 与 VFS file offset 读取到有界 kernel buffer，再 copy out，并返回 byte count。
- `SYS_CLOSE`（number=7）：关闭 process-local fd 并 drop open-file reference。

随后是 `SYS_BRK`（8）、`SYS_MAP_ANON`（9）、`SYS_FORK`（10）。只读身份/时间查询号位追加在其后，全部不阻塞、不分配、不发 EOI：

- `SYS_GET_TIME`（number=11）：返回当前墙钟时间的 Unix epoch 秒（`bigos::time::current_unix_time()`），参见 `docs/zh/arch/wall-clock-and-identity.md`。
- `SYS_GETPID`（number=12）/ `SYS_GETPPID`（number=13）：返回当前进程的 `pid` / `parent_pid`。
- `SYS_GETUID`（number=14）/ `SYS_GETGID`（number=15）：返回当前进程的 `uid` / `gid`。

Process group/session 与默认终端 foreground 控制追加在 number 41..46：
`SYS_GETPGID`、`SYS_GETSID`、`SYS_SETPGID`、`SYS_SETSID`、`SYS_TCGETPGRP` 和
`SYS_TCSETPGRP`。它们只作用于单一默认终端的有界模型，返回确定性的 POSIX 风格负
errno，不改变 `int 0x80` 寄存器 ABI、syscall vector、IDT DPL 或 EOI 规则。它们不
表示完整 POSIX job control、`tcsetpgrp(3)` 语义、`termios`、多终端、后台作业或完
整 POSIX 进程模型。

默认终端 mode control 追加在 number 51..52：`SYS_TCGETMODE` 和
`SYS_TCSETMODE`。它们用 `rdi` 传入固定 BigOS terminal-mode object 的用户指针，
只暴露单一默认 console terminal 的 canonical/raw 输入模式。`SYS_TCGETMODE`
复制确定性快照，不修改 terminal、fd、process 或 foreground state。
`SYS_TCSETMODE` 校验 object size/version/flags/mode，并要求调用者属于当前
foreground process group；session leader recovery path 只能恢复 canonical mode。
这些 syscall 是 append-only，不表示 POSIX `tcgetattr`/`tcsetattr`、完整
`termios`、baud rate、`VMIN/VTIME`、pseudo-terminal、后台读写控制或完整 job
control。

其后是 `SYS_KILL`（16）、`SYS_SIGACTION`（17）、`SYS_SIGPROCMASK`（18）、`SYS_SIGRETURN`（19）、`SYS_LSEEK`（20）、`SYS_PIPE`（21）、`SYS_DUP`（22）、`SYS_DUP2`（23）、`SYS_FSYNC`（24）、`SYS_MKDIR`（25）、`SYS_UNLINK`（26）。

- `SYS_EXECVE`（number=27）：以 append-only 方式把内核内已有的当前进程镜像替换路径（`exec_current_from_elf_image` + 只读 VFS 读路径）暴露给 CPL3。ABI：`rdi` = 用户 `path`，`rsi` = `argv`（NULL 结尾用户指针数组），`rdx` = `envp`。path 受 `SYS_PATH_MAX_LEN` 约束，argv/envp 向量受 `EXEC_MAX_ARGC` / `EXEC_MAX_ENVC` / `EXEC_MAX_STRING_BYTES` 约束；所有用户缓冲先经 VMA-backed 校验拷入再使用。成功时以新 ELF64 `ET_EXEC` 镜像替换当前进程地址空间并进入新程序入口，因此不返回调用点。失败时返回确定性负 errno（`-ENOENT`、`-EACCES`、`-ENOEXEC`、`-E2BIG`、`-EFAULT`、`-ENOMEM`、`-EWOULDBLOCK`、`-EIO`），调用镜像可继续执行。与其他 fd/VFS syscall 一样，它在分配或进入同步存储 IO 前检查 `sched::can_block()` 守卫；不改动任何既有 syscall 号、寄存器约定、`VECTOR_SYSCALL` / DPL 布局或「syscall 不发 EOI」规则。
- `SYS_READDIR`（number=28）：从已打开 fd 读取有界目录项批次到用户 `struct bigos_dirent[]`。ABI：`rdi` = fd，`rsi` = 用户 entries 缓冲区，`rdx` = 请求项数。请求受 `SYS_DIRENT_MAX_ENTRIES` 约束，经用户缓冲校验 copy out，返回 entry count 或确定性负 fd/VFS errno。
- `SYS_STAT`（number=29）/ `SYS_FSTAT`（number=30）：把有界元数据复制到用户 `struct stat`。`SYS_STAT` 使用 `rdi` = 用户 path、`rsi` = 用户输出指针；`SYS_FSTAT` 使用 `rdi` = fd、`rsi` = 用户输出指针。它们只暴露当前文件/目录元数据子集，不表示完整 POSIX `stat(2)` 语义。
- `SYS_CHDIR`（number=31）：使用 `rdi` = 用户 path；只有在完成有界路径复制、解析并确认目标为目录后，才提交进程 cwd。
- `SYS_GETCWD`（number=32）：使用 `rdi` = 用户缓冲区、`rsi` = 缓冲区大小；复制 NUL 结尾 cwd，或返回确定性 `-ERANGE`、`-EFAULT`、`-EINVAL`。
- `SYS_RENAME`（number=33）：使用 `rdi` = 旧用户 path、`rsi` = 新用户 path。它受限于当前有界可写 `/rw` regular-file rename 语义，不表示完整持久文件系统或跨设备 rename 支持。
- `SYS_MAP_FILE`（number=35）：使用 `rdi` = fd、`rsi` = 页对齐文件偏移、`rdx` = 页对齐长度、`r10` = 权限、`r8` = 保留 flags（必须为 0）。它在用户 file-mapping 窗口发布一个有界只读、私有、惰性分页的 file-backed 映射，返回映射基址用户地址，或确定性负 errno（`-EBADF`/`-EACCES`/`-EINVAL`/`-ENOMEM`/`-EWOULDBLOCK`）。fd 必须指向可读 regular file，权限必须只读且非 W+X，失败时不发布部分 VMA。覆盖页在首次读访问时经 page/buffer cache 物化；对只读页的写访问、越界访问以及在不可阻塞上下文发起缓存装入均为确定性 kill。
- `SYS_UNMAP_ANON`（number=36）：使用 `rdi` = 页对齐用户地址、`rsi` = 页对齐非零长度。它只接受用户低半区内由兼容 private anonymous VMA 完整覆盖的范围。成功时删除或拆分受影响 VMA，清除 present user PTE，通过 frame-reference helper 释放 owned/COW-shared frame，按 ownership metadata 回收空的动态用户页表页，并 invalidation 受影响的当前 CPU translation。
- `SYS_PROTECT_ANON`（number=37）：使用 `rdi` = 页对齐用户地址、`rsi` = 页对齐非零长度、`rdx` = BigOS 权限 bit（`Read=1`、`Write=2`、`Execute=4`）。它只接受兼容 private anonymous VMA，拒绝 W+X 与 unsupported backing，先 staging 所需 VMA split 再发布 metadata，并更新 present PTE 权限，使页表权限不宽于 VMA policy。
- `SYS_RMDIR`（number=38）：使用 `rdi` = 用户路径，在可写 `/rw` 后端删除空目录。它会以确定性负错误码拒绝常规文件、非空目录、缺失路径、只读后端目标、非法用户路径和不可阻塞上下文。
- `SYS_FTRUNCATE`（number=39）：使用 `rdi` = fd、`rsi` = 有界长度。它只接受可写
  `/rw` 常规文件，成功时更新 size metadata，让扩展范围读取为零，并把被截断尾部块
  安全释放给复用集合；目录、只读后端、过大长度、非法 fd 和不可阻塞上下文都会返回确定性负错误码。
- `SYS_WAITPID`（number=47）：追加的有界 wait 变体。ABI：`rdi` = `WAIT_ANY` 或正 child pid，`rsi` = 可选用户 `int*` status 输出，`rdx` = options。仅支持 `options == 0` 与 `WNOHANG`；process-group selector、stopped/continued 状态、resource usage 与完整 POSIX job-control 语义仍不支持。`WNOHANG` 在存在匹配 child 但当前没有可回收 zombie 时返回 `0`。
- `SYS_FCNTL`（number=48）：有界 fd-control 入口，支持 `F_GETFD`、带 `FD_CLOEXEC` 的 `F_SETFD` 和 `F_DUPFD`。`F_DUPFD` 返回不小于调用方最小值的最低可用 fd，并清除新 descriptor 的 close-on-exec。它不实现 record locking、nonblocking I/O、async I/O、descriptor passing 或完整 POSIX `fcntl(2)`。
- `SYS_ACCESS`（number=49）：通过共享 VFS path resolution 与 metadata 执行有界路径可见性/权限检查。仅支持 `F_OK`、`R_OK`、`W_OK`、`X_OK` bit，unsupported bit 确定性失败，且不打开或发布 descriptor。
- `SYS_TRUNCATE`（number=50）：按路径执行有界 truncate，语义与 `SYS_FTRUNCATE` 的可写 `/rw` regular-file 子集一致。只读后端目标、目录、缺失路径、非法路径、过大长度和不可阻塞上下文都会失败，且不发布部分 size 更新。
- `SYS_UTIMENS`（number=54）：按路径执行有界时间戳更新。ABI：`rdi` = 用户 path，`rsi` = atime 秒，`rdx` = mtime 秒，`r10` = BigOS atime/mtime NOW 或 OMIT flags。它只面向受支持的可写 `/rw` 对象，成功时把 ctime 更新为当前有界 wall-clock 秒；unsupported flags、只读后端等失败确定性返回且不修改时间戳。它不实现 POSIX `utimensat`、`futimens`、symlink 时间戳、纳秒精度、时区转换或 directory-fd 相对路径。

这些 lifecycle syscall 是有界 BigOS 操作，不是完整 POSIX `munmap`、`mprotect`、完整文件大小管理或完整 POSIX 文件时间戳管理。它们不支持 VM 操作的任意字节粒度、`MAP_FIXED` 覆盖、shared writable mapping、file-backed writable upgrade、sparse-file API、journal、power-loss recovery、swap 或跨 CPU TLB shootdown。

最小用户可见 UDP socket 接口追加在 number 55..58：

- `SYS_SOCKET`（number=55）：ABI `rdi` = domain，`rsi` = type，`rdx` = protocol。它只接受有界 BigOS UDP 子集（`SOCKET_AF_INET`、`SOCKET_SOCK_DGRAM`、protocol `0`/`SOCKET_IPPROTO_UDP`），在单一内核内部默认网络 context 上创建未绑定 socket `vfs::File` backend，安装到 fd 表并返回进程本地 fd 或确定性负 errno（`-EINVAL`/`-ENODEV`/`-ENOMEM`/`-EMFILE`）。socket fd 复用既有 `close`/`dup`/`dup2`/`fcntl`/`close-on-exec`/`fork` 路径。
- `SYS_BIND`（number=56）：ABI `rdi` = socket fd，`rsi` = 用户 `struct SockAddrIn*`，`rdx` = addrlen。`addrlen` 必须等于 `sizeof(SockAddrIn)`，`family` 必须为 `SOCKET_AF_INET`。它通过内核内部 UDP API 绑定本地端口，并把协议结果（`AlreadyBound`/`TableFull`/`InvalidArgument`）映射为确定性 errno。
- `SYS_SENDTO`（number=57）：ABI `rdi` = fd，`rsi` = 用户缓冲，`rdx` = 长度，`r10` = 用户 `struct SockAddrIn*` 目的地址，`r8` = addrlen。payload 受 `SYS_IO_MAX_LEN`/`UDP_MAX_PAYLOAD` 约束；它通过 VMA-backed 校验拷贝有界 payload 与目的地址，经内核内部 UDP API 发送，并返回字节数或确定性 errno。
- `SYS_RECVFROM`（number=58）：ABI `rdi` = fd，`rsi` = 用户缓冲，`rdx` = 长度，`r10` = 可选用户 `struct SockAddrIn*` 来源输出，`r8` = 可选 `uint32_t*` addrlen in/out。它执行有界 `pump` 加轮询 RX 推进与有界让出等待，把一个 datagram 的 payload 与来源 IPv4/port 写回用户态，在有界等待内无 datagram 时返回 `-EAGAIN`。这是有界、非通用 POSIX 阻塞契约。socket `read`/`write` 刻意返回 `-EOPNOTSUPP`；数据仅经 `sendto`/`recvfrom` 流动。

这些 socket syscall 是内核内部协议路径之上的有界 UDP 适配层，不是完整 POSIX socket 层：没有 TCP/stream socket、`connect`/`listen`/`accept`/`shutdown`、`getsockopt`/`setsockopt`、`poll`/`select`、scatter-gather、ancillary data、完整 `AF_*`/`SOCK_*` 矩阵、DHCP、DNS、IPv6 或多 context/多网卡选择。

syscall dispatcher 保持 exception/IRQ/syscall 的 EOI 分离不变。CPU exception 与外部 IRQ 仍是 nonblocking context。fd/VFS syscall 在分配或进入同步 ATA PIO/exFAT read 前检查 `sched::can_block()`；普通用户进程 syscall 可通过该 guard，因为 DPL=3 trap gate 会保留 IF。

用户态 raw syscall primitive `syscall0` 到 `syscall6` 仍是 BigOS-specific 低层 helper。它们把 number 与返回值绑定到 `rax`，参数绑定到 `rdi`、`rsi`、`rdx`、`r10`、`r8`、`r9`，并列出 `rcx`、`r11` 与 `memory` clobber。源码级 contract 测试会检查这些约束，避免 wrapper 修改静默偏离 ABI；更高层 libc wrapper 仍负责把内核负返回值翻译为正 `errno` 与接口文档化的失败哨兵。

## 验证：默认关闭构建开关 + 确定性 marker

默认关闭的 xmake 开关 `syscall_smoke`（`xmake f --syscall_smoke=y`）继续从 ring0 验证 `SYS_DEBUG_WRITE`、`SYS_GET_TICK` 和未知 number。额外默认关闭的 smoke 覆盖 flat 首个用户程序、filesystem-backed user ELF、demand paging、有界只读 file-backed 映射（`xmake f --file_backed_mapping_smoke=y`，marker `BIGOS_FILE_BACKED_MAPPING_PASSED`/`FAILED`）、anonymous map/unmap/protect lifecycle（`xmake f --anonymous_lifecycle_smoke=y`，marker `BIGOS_ANON_LIFECYCLE_PASSED`/`FAILED`）、fork/COW、time/identity、signals、writable FS、pipes 和 userland runtime。userland runtime smoke 也断言代表性的 process-group/session 与 foreground-terminal wrapper 行为。普通启动现在会打包 `/boot/user/init.elf`，进入常驻 PID-1 init，并启动 `/bin/sh`；默认 headless 验证观察 `BIGOS_USER_EXEC`。

## 本阶段非目标

- 不切换到 `syscall`/`sysret` MSR 快速路径。
- 不把当前 bounded syscall 集解释为完整 POSIX-wide syscall 语义、用户线程、作业控制、动态链接或完整 libc。
- 不把 demand paging/COW 扩展到当前 bounded anonymous mapping 与有界只读 file-backed 映射之外，也不引入可写/写回或共享 file-backed `mmap`。
- 不对 syscall 以外的 IDT gate 放宽 DPL；不从 syscall path 发送 i8259 EOI。

## 横切工程化项

本 change 未修改 `tools.bigosdev`。若后续需要它自动注入 `syscall_smoke` 开关并观测 `BIGOS_SYSCALL_*` marker，应作为单独的横切工程化项处理，不把 Python 修改混入本 change，除非明确扩展任务范围。
