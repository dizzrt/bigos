## Context

内核已落地统一 fd 就绪（readiness）模型（归档 change `fd-readiness-model`）：`vfs::poll_file(File*)` 经 `FileOperations.poll` op 派发，pipe/socket/tty 各自实现的 `poll` op 复用与阻塞读写**同源**的谓词（`read_ready`/`write_ready`、socket `rx_count`、tty `input_available`/`input_record_available`），返回 `READY_READABLE`/`READY_WRITABLE`/`READY_ERROR` 位标志。

当前可阻塞描述符的读写在“将要阻塞”的判定点只有一种非阻塞出口——`!bigos::sched::can_block()` 时返回 `WouldBlock`：

- pipe（`kernel/core/ipc/pipe.cc`）：`pipe_read` 在 `count == 0 && write_open` 时，若 `!can_block()` 返回 `Status::WouldBlock`，否则 `wait_queue_wait_until(&read_wq, read_ready, ...)`；`pipe_write` 在 `count == CAPACITY` 时对称处理。
- tty（`kernel/core/terminal/tty.cc`）：读路径在无输入时阻塞于 `g_input_wait`，同样有 `!can_block()` 短路。
- socket（`kernel/core/syscall/syscall.cc` 的 `sys_recvfrom`）：先 `!can_block()` → `-EWOULDBLOCK`，否则做最多 `RECV_MAX_ROUNDS` 轮 `pump + udp_receive_from + yield` 的有界等待，最终无数据返回 `-EAGAIN`。

fd-control 路径已存在：`SYS_FCNTL`（编号 48）→ `proc::fcntl_fd_current(fd, cmd, arg)`，当前支持 `FCNTL_F_GETFD`/`FCNTL_F_SETFD`/`FCNTL_F_DUPFD`（`include/bigos/proc.h` 定义常量）。`FdEntry { vfs::File *file; bool close_on_exec; bool readable; }` 是**进程局部**的描述符表项；`vfs::File` 是**跨进程共享**的 open file description（`dup`/`fork` 经 `vfs::retain` 共享同一 `File`，引用计数回收）。`vfs::File` 末尾已有追加字段先例（`bool writable;`）。

errno 单一来源 `include/bigos/errno.h`，`EWOULDBLOCK == EAGAIN == 11`。用户 libc 的 `fcntl` wrapper（`user/libc/syscall.c`）与常量头（`user/libc/include/fcntl.h`）当前只暴露 `F_DUPFD/F_GETFD/F_SETFD` 与 open flag。

本变更是 M13 深度阶段第二步：在统一就绪模型之上，让读写在数据未就绪时返回确定性 would-block 而非阻塞，并通过既有 fd-control 路径开关该行为。

## Goals / Non-Goals

**Goals:**

- 在 open file description（`vfs::File`）粒度提供一个非阻塞标志，默认关闭；`dup`/`fork` 共享同一 `File` 时该标志天然共享。
- 不新增 syscall 编号，在既有 `SYS_FCNTL` 上新增 `F_GETFL`/`F_SETFL`：`F_GETFL` 返回访问模式 + `O_NONBLOCK` 快照；`F_SETFL` 仅可切换 `O_NONBLOCK`，对其它标志位确定性处理（忽略不支持位或拒绝非法位）。
- 非阻塞标志置位时，pipe/tty 的读写在“将要阻塞”的判定点直接返回 would-block（`WouldBlock` → 用户层 `-EWOULDBLOCK`），socket recvfrom 在一次有界 RX 推进后无数据立即返回 `-EAGAIN`，**不进入等待队列、不做 yield 轮转**。
- would-block 判定与就绪谓词、阻塞谓词三者**同源**：非阻塞返回 would-block 当且仅当阻塞路径此刻会进入等待（即 `poll_file` 报告不可读/不可写）。
- 内核与用户两份 `O_NONBLOCK`/`F_GETFL`/`F_SETFL` 定义一致；新增默认关闭 smoke 验证。

**Non-Goals:**

- 不实现完整 POSIX `O_NONBLOCK`：常规文件、块设备、目录等恒定就绪/同步完成的描述符类型不改变行为（`F_SETFL` 置位对它们无可观察阻塞影响，读写仍按既有同步语义返回）。
- 不实现 `O_ASYNC`/信号驱动 I/O、记录锁、`F_DUPFD_CLOEXEC`、描述符传递。
- 不实现用户态多路复用 syscall（`poll`/`select` 类，属 M13.3）。
- 不改动既有 syscall 编号取值与 `int 0x80` ABI、boot/链接脚本/IDT/syscall 向量/页表/磁盘布局。
- 不把 `O_NONBLOCK` 接入 `open()` 时的初始标志解析（仅经 `F_SETFL` 设置）；如需 open-time 置位作为可选内聚项，不改变 `F_SETFL` 的主路径语义。

## Decisions

### 决策一：非阻塞标志存放在 `vfs::File`（open file description 粒度），而非 `FdEntry`

在 `vfs::File` 末尾**追加**一个布尔字段（暂命名 `nonblocking`），默认 `false`。

- 备选 A：放在 `proc::FdEntry`（进程局部）。否决：`dup`/`dup2`/`fork` 复制的是 fd 表项，会导致同一 open file description 的不同 fd 持有不一致的非阻塞标志，违背 POSIX“标志属于打开文件描述”的语义，也与既有 `close_on_exec`（确实属于 fd 表项）混淆层次。
- 备选 B：放在各后端 `private_data`（pipe/socket 各存一份）。否决：tty 的 `private_data` 由所有终端 fd 共享同一 `TTY_OPS` 句柄，无法表达“某个 fd 非阻塞、另一个阻塞”；且要求每个后端各加字段、各写读取逻辑，分散且易漂移。
- 选择 `vfs::File` 的理由：与 readiness op 一样，把非阻塞作为打开文件描述的统一属性；`dup`/`fork` 经 `vfs::retain` 共享 `File` 即共享标志，符合 POSIX；只追加一个字段、不重排既有布局（沿用 `bool writable;` 先例），以 `_Static_assert`/源级核对守护。
- tty 的联动语义（已确认）：在真实 userland 安装路径（`kernel/core/proc/proc.cc` 的 stdio 安装）中，fd 0/1/2 由一次 `create_tty_file()` 安装后，fd 1/2 各 `vfs::retain`，三者**共享同一 `vfs::File`**（ref_count == 3）。因此对 fd 0/1/2 任一调用 `F_SETFL(O_NONBLOCK)` 会**联动**全部终端 fd（含 `dup` 副本），这与 POSIX“`O_NONBLOCK` 属于 open file description、`dup`/`fork` 共享同一 OFD”的标准语义一致（Linux 上 0/1/2 常指向同一次终端 open 亦呈现相同联动）。本变更把该联动作为**确定的、文档化的契约**，由 smoke 显式验证，不改变安装方式，也不为追求“独立标志”把标志下放到 `FdEntry`。需要独立阻塞性的程序应自行重新打开终端获得新的 OFD。

### 决策二：在 `SYS_FCNTL` 扩展 `F_GETFL`/`F_SETFL`，不新增 syscall 编号

在 `include/bigos/proc.h` 新增 `FCNTL_F_GETFL`/`FCNTL_F_SETFL` 命令常量（取值不与既有 `F_DUPFD=0/F_GETFD=1/F_SETFD=2` 冲突，如 3/4），并新增 `O_NONBLOCK` 常量（取值与 `O_RDONLY/O_WRONLY/O_RDWR/O_CREAT/O_TRUNC` 不冲突，建议沿用常见 `1 << 11` 或项目内未占用位）。`proc::fcntl_fd_current` 增补：

- `F_GETFL`：返回该 `File` 的访问模式位（由 `readable`/`writable` 合成 `O_RDONLY`/`O_WRONLY`/`O_RDWR`）按位或 `O_NONBLOCK`（若置位）。
- `F_SETFL`：只读取 `arg` 中的 `O_NONBLOCK` 位写入 `File.nonblocking`，其余所有位（访问模式 `O_RDONLY`/`O_WRONLY`/`O_RDWR`、创建位 `O_CREAT`/`O_TRUNC`、以及任何 BigOS 未实现的 status 位）一律忽略且不报错、返回 0（与 Linux/BSD/POSIX 一致，POSIX 本身即要求忽略访问模式位与创建位）。该策略是唯一不会打挂标准惯用法 `flags = F_GETFL; F_SETFL(flags | O_NONBLOCK)` 的选择（其 `arg` 必然携带访问模式位）。实现为单条 `nonblocking = (arg & O_NONBLOCK) != 0`，不触碰访问模式与 `FdEntry.close_on_exec`。

`sys_fcntl` 的可阻塞 guard：现有代码仅对 `F_DUPFD`（可能增长 fd 表/分配）检查 `can_block()`；`F_GETFL`/`F_SETFL` 是纯标志读写、无分配、无阻塞，不需要该 guard。

- 备选：新增独立 `SYS_FCNTL_FL` 编号或新 syscall。否决：与项目“不新增 syscall 编号、复用 fd-control 路径”的约束冲突，且 `F_GETFL`/`F_SETFL` 天然属于 `fcntl` 命令族。

### 决策三：非阻塞读写在“将要阻塞”的判定点短路，复用既有 `!can_block()` 出口

各后端已有的阻塞判定点正是非阻塞短路点。统一规则：**当且仅当此刻需要进入等待**时，若描述符非阻塞或不可阻塞，返回 would-block：

- pipe：`pipe_read` 在 `count == 0 && write_open`、`pipe_write` 在 `count == CAPACITY && read_open` 时，把现有 `if (!can_block())` 条件扩展为 `if (file_is_nonblocking(file) || !can_block())`，返回 `Status::WouldBlock`（写已写出部分则返回已写字节，沿用现有 `done > 0` 逻辑）。
- tty：读路径无输入时同样以 `file_is_nonblocking(file) || !can_block()` 短路返回 `WouldBlock`；终端写出方向恒可写，非阻塞不改变其行为。
- socket（`sys_recvfrom`）：若 fd 非阻塞，则 `RECV_MAX_ROUNDS` 取 1（只做一次 `pump + udp_receive_from`，不 `yield`）；无数据返回 `-EAGAIN`，与阻塞路径最终无数据的返回码一致。阻塞 socket fd 维持现有有界等待轮次不变。

`WouldBlock` 经既有 `vfs::Status` → errno 翻译路径映射为 `-EWOULDBLOCK`（== `-EAGAIN`），用户可见返回码确定。

- 同源保证：短路判定使用与 `poll_file`/阻塞谓词相同的条件（pipe 用 `read_ready`/`write_ready` 同一表达式，tty 用 `input_available`），禁止各写一份，避免“`poll` 说可读、非阻塞读却返回 would-block”的不一致。

### 决策四：would-block 与就绪查询的一致性契约

定义不变式：对同一描述符在同一时刻，`poll_file(file)` 报告 `READY_READABLE` ⇔ 非阻塞读不会返回 would-block（即有数据或 EOF）；`READY_WRITABLE` ⇔ 非阻塞写不会返回 would-block。smoke 直接交叉验证该不变式（查询就绪后非阻塞读/写立即成功；查询不就绪则非阻塞读/写返回 would-block）。这是 M13.3 多路复用可正确驱动非阻塞 fd 的前提。

### 控制流 / 数据流

非阻塞设置（同步、线程上下文）：

```
user fcntl(fd, F_SETFL, O_NONBLOCK)
  -> int 0x80 (SYS_FCNTL) -> sys_fcntl -> proc::fcntl_fd_current(F_SETFL)
  -> FdEntry.file->nonblocking = (arg & O_NONBLOCK) != 0
```

非阻塞读（数据未就绪）：

```
user read(fd) -> SYS_READ -> proc::read_fd_current -> vfs::read -> ops->read(pipe/tty)
  -> 判定点: count==0 && write_open(或无 tty 输入)
     -> file->nonblocking || !can_block()  => return WouldBlock(-EWOULDBLOCK)
     -> 否则 wait_queue_wait_until(...)   (既有阻塞路径)
```

非阻塞 recvfrom（数据未就绪）：

```
user recvfrom(fd) -> SYS_RECVFROM -> sys_recvfrom
  -> nonblocking ? rounds=1 : rounds=RECV_MAX_ROUNDS
  -> pump + udp_receive_from [+ yield 仅阻塞路径]
  -> NoData => -EAGAIN
```

唤醒侧（生产者，可能在 IRQ/投递上下文）保持不变：pipe 写/读/关闭、tty 输入入队、net RX 投递仍 `wake_*`，非阻塞读者本就未在等待队列，不受影响。

### 失败行为

- 非阻塞读/写无数据/缓冲满：返回 `WouldBlock`/`-EWOULDBLOCK`（== `-EAGAIN`），不进入等待、不消费/产生数据；写已写出部分按既有 `done > 0` 返回已写字节。
- `F_SETFL`/`F_GETFL` 对 bad fd：沿用 `fcntl_fd_current` 既有 `-EBADF`。
- `F_SETFL` 携带不支持位：忽略不支持位（默认策略），不分配、不改其它 fd 状态。
- 不可阻塞上下文（IRQ/调度临界）下读写需要等待：与非阻塞同样返回 would-block（既有行为，保持不变）。
- 低层分配/IO/中断路径不被本变更触及：非阻塞只在判定点短路，不新增分配，不进入 IRQ 路径。

## Risks / Trade-offs

- [非阻塞标志层次放错（fd vs open file description）导致 dup/fork 行为不符 POSIX] → 标志存于 `vfs::File`，经 `retain` 共享；tty fd 0/1/2 共享同一 `File` 已确认，联动语义作为文档化契约并由 smoke 显式断言。
- [would-block 谓词与就绪/阻塞谓词漂移] → 三者强制复用同一表达式（pipe `read_ready`/`write_ready`、tty `input_available`、socket `rx_count`/`udp_receive_from`）；smoke 交叉断言“查询就绪 ⇔ 非阻塞读写不 would-block”。
- [`vfs::File` 追加字段影响既有布局/后端] → 仅末尾追加一个布尔，沿用 `bool writable;` 先例；以 `_Static_assert`/源级核对守护偏移；既有后端零改动，默认 `false` 保持阻塞语义。
- [`F_SETFL` 误改访问模式或误置 cloexec] → `F_SETFL` 只读取 `O_NONBLOCK` 位写入，明确忽略访问模式位，不触碰 `FdEntry.close_on_exec`。
- [socket recvfrom 改 rounds 影响阻塞路径] → 仅当 fd 非阻塞时令 `rounds=1` 且跳过 `yield`；阻塞 fd 的 `RECV_MAX_ROUNDS`/`yield` 行为与返回码（无数据 `-EAGAIN`）保持不变。
- [内核/用户两份常量不一致] → `O_NONBLOCK`/`F_GETFL`/`F_SETFL` 在内核与 `user/libc` 两份定义，沿用既有源级契约校验（pytest）确保相等且与 open flag 不冲突。
- [默认启动行为变化] → 非阻塞默认关闭，所有现有阻塞读写路径默认不变；smoke 默认关闭，不影响默认 boot 进 shell。

## Migration Plan

1. VFS：在 `include/bigos/fs/vfs.h` 的 `vfs::File` 末尾追加 `bool nonblocking`（默认 false），加布局守护；提供内核内辅助（如 `bool file_is_nonblocking(File*)`/读写访问模式合成），并定义内核内 `O_NONBLOCK` 常量来源。
2. fd-control：在 `include/bigos/proc.h` 新增 `FCNTL_F_GETFL`/`FCNTL_F_SETFL` 与 `O_NONBLOCK`；在 `kernel/core/proc/proc.cc` 的 `fcntl_fd_current` 实现 `F_GETFL`（合成访问模式 | 非阻塞位）与 `F_SETFL`（仅写非阻塞位）；`sys_fcntl` 对新命令不加 `can_block` guard。
3. pipe：在 `kernel/core/ipc/pipe.cc` 的 `pipe_read`/`pipe_write` 判定点把短路条件扩展为 `nonblocking || !can_block()`。
4. tty：在 `kernel/core/terminal/tty.cc` 读路径判定点同样扩展短路条件；fd 0/1/2 共享同一 `File`（已确认），联动语义保持不变。
5. socket：在 `kernel/core/syscall/syscall.cc` 的 `sys_recvfrom` 按 `nonblocking` 选择 `rounds=1` 且跳过 `yield`，无数据 `-EAGAIN`。
6. 用户 libc：在 `user/libc/include/fcntl.h` 增补 `O_NONBLOCK`/`F_GETFL`/`F_SETFL`；确认 `fcntl` wrapper 对新 cmd 的可变参透传（`F_SETFL`/`F_GETFL` 的 arg 处理）。
7. 验证：新增默认关闭 xmake smoke 开关 → `BIGOS_*` 宏 → `kernel.cc` smoke 入口线程，对 pipe/tty/socket 断言非阻塞 would-block 与就绪一致性、`F_GETFL`/`F_SETFL` 往返、以及终端 fd 联动（对 fd 1 设非阻塞后 fd 0 与 `dup` 副本同步可见），发 COM1 `BIGOS_*_PASSED`/`_FAILED`；更新源级 syscall/fcntl 常量一致性校验；QEMU headless 验证。
8. 文档：`docs/en` 更新非阻塞描述符模型说明并在 `docs/zh` 同步对应相对路径镜像。

回滚策略：本变更为追加式、默认关闭、不改默认启动与既有 ABI 编号。回滚时移除 `vfs::File.nonblocking` 字段、`F_GETFL`/`F_SETFL` 分支与各后端短路条件扩展即可，既有阻塞读写与 `sys_recvfrom` 对外语义不受影响。

## Open Questions

无未决问题。以下两项已定稿，见 Resolved Questions。

## Resolved Questions

- `F_SETFL` 对标志位的处理：**已确认采用掩码到 `O_NONBLOCK` 的宽容策略**。`F_SETFL` 只读取 `arg` 的 `O_NONBLOCK` 位写入 OFD，其余所有位（访问模式 `O_RDONLY`/`O_WRONLY`/`O_RDWR`、创建位 `O_CREAT`/`O_TRUNC`、以及任何 BigOS 未实现的 status 位）一律忽略且不报错、返回 0；bad fd 仍按既有 `-EBADF`。理由：这是唯一不会打挂标准惯用法 `flags = F_GETFL; F_SETFL(flags | O_NONBLOCK)` 的策略（该惯用法的 `arg` 必然携带访问模式位），与 Linux（`setfl` 把 `arg` 掩码到可改 status flags、丢弃其余位）/BSD/POSIX 一致；POSIX 本身即要求 `F_SETFL` 忽略访问模式位与创建位。BigOS 不向用户暴露 `O_APPEND`/`O_ASYNC` 等常量，静默 no-op 风险极小，且实现为单条 `nonblocking = (arg & O_NONBLOCK) != 0`，确定性强。该行为由 spec 场景（携带访问模式位的 `F_SETFL` 仍成功且只改非阻塞位）与 smoke 显式钉死。
- tty 终端 fd（0/1/2 与 `dup` 副本）的非阻塞标志作用域：**已确认为联动**。真实 userland 安装路径中 fd 0/1/2 共享同一 `vfs::File`（一次 `create_tty_file()` + fd 1/2 各 `retain`，ref_count == 3），因此对任一终端 fd 设置 `O_NONBLOCK` 影响全部共享同一 OFD 的终端 fd（含 `dup` 副本）。该行为与 POSIX 共享 OFD 语义一致，作为文档化契约由 smoke 显式验证；不改变安装方式、不下放标志到 `FdEntry`。
