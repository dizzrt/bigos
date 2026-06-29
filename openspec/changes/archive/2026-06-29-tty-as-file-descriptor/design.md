## Context

终端在当前内核里是一个全局单例，而非 `vfs::File`：

- 设备状态集中在 `kernel/core/terminal/tty.cc` 的文件作用域全局：输入环 `g_input`（`TTY_INPUT_CAPACITY = 128`）与等待队列 `g_input_wait`；键盘 IRQ 生产者 `enqueue_input*` 入环后 `wake_one(&g_input_wait)`；非中断消费者 `read_char_blocking`/`read_raw_available_blocking`/`read_input_record_blocking` 在 `g_input_wait` 上等待。
- 标准描述符没有 File 对象：`init_fd_table`（`kernel/core/proc/proc.cc`）把 `fd_table` 置空、`fd_capacity = 0`；fd 0/1/2 没有任何 `vfs::File`。
- 终端读写靠两处裸 fd 特例：`sys_read` 对 `fd==0 && file_for_fd_current(0)==nullptr` 直读全局 tty；`sys_write` 对 `(fd==1||fd==2) && file_for_fd_current(fd)==nullptr` 直写默认控制台，并在该路径内发 `BIGOS_USER_WRITE_SYSCALL` 等 COM1 marker（headless 验证依赖）。
- 其它描述符（常规文件、pipe、socket）统一经 `file->ops` 派发，类型用 ops 指针标识（`is_pipe_file`/`is_socket_file`）。

问题：终端是派发体系里唯一的“裸 fd 例外”。这让“对每个 fd 一致调 `ops`”的诉求（统一 readiness、后续多路复用）必须为终端反复写特例。本变更把终端收编为标准 `vfs::File`。

参考实现（Linux）：Linux 把“打开句柄”和“设备”彻底分两层——per-open 的 `struct file`（带 `f_count` 引用计数、`f_op`、`f_pos`）通过 `file->private_data` 指向**全设备唯一**的 `struct tty_struct`（带 `read_wait`/`write_wait` 等待队列与输入缓冲）。`dup`/`fork` 共享同一 `struct file` 并 `f_count++`，`close` 归零即释放句柄；设备对象长期存在。`tty_poll` 在 `f_op->poll` 里 `poll_wait(file, &tty->read_wait)` 并返回就绪掩码。本设计直接映射这套分层。

## Goals / Non-Goals

**Goals:**

- 把全局终端表达为标准 `vfs::File` 句柄，使 fd 0/1/2 成为真正的 fd 表项。
- 终端读写经统一 `file->ops` 派发，消除 `sys_read`/`sys_write` 的裸 fd 特例分支。
- tty `vfs::File` 走与 pipe/socket 一致的 `retain`/`release` 引用计数，无“单例 File 禁止释放”特例。
- 原样保留 headless 验证 marker 的文本与时机，保持默认启动可见行为与终端阻塞/输入行为不变。
- 为后续统一 readiness（M13.1）预留 `TTY_OPS.poll` 接入点，使终端 readiness 不再需要裸 fd 桥接。

**Non-Goals:**

- 不实现完整 POSIX tty：termios 全集、完整行规程、SIGTTIN/SIGTTOU、超出既有 `foreground_pgid` 的控制终端语义。
- 不引入多路复用 syscall（M13.3）与非阻塞标志（M13.2）。
- 不改键盘 IRQ 输入路径、控制台渲染、scrollback 或 marker 文本。
- 不改 boot 地址、链接脚本、IDT/syscall 向量、页表布局、磁盘布局或 syscall ABI。

## Decisions

### 决策一：设备/句柄两层模型（采用 Linux 形态）

设备层 = 现有全局 tty（`g_input`/`g_input_wait`，静态、长期存在、永不 `free`）。句柄层 = 新增 `vfs::File`，`kmalloc` 分配、`private_data` 指向全局 tty、`vnode = nullptr`。两层通过 `private_data` 连接。

- 等待队列、输入缓冲保留在设备层，不迁移到 File；句柄层只是薄包装。
- 备选：把 tty 状态搬进 File（每 File 一份输入环/等待队列）。否决：键盘 IRQ 只认一个全局输入目标，多份缓冲会割裂输入；且与现有 IRQ 生产者耦合大。

### 决策二：tty File 走标准引用计数，不开释放特例

tty File 与 pipe/socket 一样经 `vfs::retain`/`vfs::release`，`ref_count==0` 时释放 File 结构本身；`TTY_OPS.close` 对设备层为 no-op（不动 `g_input`）。

- 这正是 Linux 分层带来的红利：句柄可自由创建/释放，设备恒在，无需“这个 File 不能 free”的特例（否则会与 `release→free` 约定冲突）。
- 备选：全局单例 File。否决：`release` 在 `ref==0` 会 `free` 静态对象，必须特判，增加脆弱面。

### 决策三：fd 0/1/2 共享同一 tty File（一次构造，retain 至 ref_count=3）

进程创建时构造一个可读可写（`readable = writable = true`）的 tty File，安装到 fd 0/1/2，三者指向同一 File、`ref_count` 增到 3。

- 贴合真实形态：shell 中 0/1/2 常为同一 open 后 `dup` 出；BigOS 既有 `dup` 已是共享 File 语义。tty 忽略 `offset`，共享无副作用。
- 备选：三个独立 File。否决：多分配、无收益，且要维护三份相同状态指针。

### 决策四：收编裸 fd 特例，marker 移入 write op 内原样保留

删除 `sys_read`(fd 0)、`sys_write`(fd 1/2) 的 `file_for_fd_current(...)==nullptr` 特例分支；终端读写统一走 `read_fd_current`/`write_fd_current` → `file->ops`。`TTY_OPS.write` 内部按现状顺序发 `BIGOS_USER_WRITE_SYSCALL` + 内容到 COM1，再 `default_terminal_write`，地址空间切换逻辑保持等价。

- marker 文本、顺序、长度上限（`SYS_WRITE_MAX_LEN`）保持不变，避免 headless 回归。
- 备选：保留特例、仅另加 File。否决：违背本变更目的（统一派发），且留双路径。

### 控制流 / 数据流

安装（进程创建，线程上下文）：

```
create process -> init_fd_table(置空) -> 构造 tty File(private_data=&g_tty, RDWR)
              -> install fd0/fd1/fd2 共享同一 File (ref_count -> 3)
```

读（`sys_read` 收编后，仅在 can_block 线程上下文）：

```
sys_read(fd) -> read_fd_current(fd) -> file->ops->read (TTY_OPS.read)
            -> read_char_blocking / read_raw_available_blocking
            -> 在 g_input_wait 等待 (既有阻塞语义)
```

写（`sys_write` 收编后）：

```
sys_write(fd) -> write_fd_current(fd) -> file->ops->write (TTY_OPS.write)
             -> serial_puts("BIGOS_USER_WRITE_SYSCALL") + serial_puts(content)
             -> 地址空间切换 -> default_terminal_write -> 还原地址空间
```

输入唤醒（键盘 IRQ，生产侧，不变）：

```
keyboard IRQ -> enqueue_input* -> wake_one(&g_input_wait)
```

### 失败行为

- tty File 构造时 `kmalloc` 失败：进程创建按既有失败路径处理（与 fd 安装失败一致地返回错误/清理），不 panic；需保证已安装的 fd 在清理路径被正确 `release`。
- 安装到 fd 0/1/2 过程中途失败：回滚已安装项（`release` 对应 retain），保持 fd 表一致。
- 终端写时地址空间切换：保持现状等价逻辑；marker 写 COM1 不依赖用户地址空间。
- `close`/`fork`/`dup` 的引用计数：`close` 归零仅释放 File 结构、设备层不受影响；`fork` 复制按既有 `retain` 路径；不得出现对全局设备的重复释放。

## Risks / Trade-offs

- [marker 时机/文本漂移导致 headless 回归] → write op 内逐字节照搬现有 `sys_write` fd 1/2 分支的 marker 与地址空间切换顺序；以默认启动 headless（期待既有 marker）回归核对。
- [tty File 生命周期错误导致重复释放/泄漏] → `TTY_OPS.close` 对设备 no-op，仅句柄结构经标准 `release` 释放；在进程退出/`fork`/`dup`/`exec`(`close_on_exec`) 路径核对引用计数；必要时加轻量计数断言。
- [进程创建路径众多（约 5 处 `init_fd_table` 调用）遗漏安装] → 把“安装标准 fd”收敛到单一 helper，在用户进程创建路径统一调用；smoke 注入进程按需决定是否安装（保持有界）。
- [`close_on_exec`/exec 后标准 fd 丢失] → 标准 tty fd 默认 `close_on_exec=false`；如 exec 重建 fd 表，统一 helper 负责重新安装，确保 exec 后 0/1/2 仍可用。
- [改动 syscall 关键路径影响所有用户输出] → 保留 `can_block`/缓冲上限/`EFAULT` 校验等既有约束；改动限定为“分发入口从特例改为 ops”，不改语义。
- [源码测试与新结构不符] → 同步更新 `tests/test_tty_console_input_source.py`，断言新派发结构与保留的 marker（用 `uv run pytest` 验证）。

## Migration Plan

1. VFS/terminal：定义 `TTY_OPS`（`read`/`write`/`close`，预留 `poll` 接入点）与终端 File 构造 helper，复用既有 `read_char_blocking`/`read_raw_available_blocking`/`default_terminal_write`；write op 内保留 marker 与地址空间切换。
2. proc：新增“安装标准 fd 0/1/2”helper（共享同一 tty File，retain 至 ref_count=3，`close_on_exec=false`），在用户进程创建路径统一调用；核对 `fork` 复制与 `close_on_exec` 路径。exec 不重建 fd 表（仅 `close_on_exec_fds`），标准 fd 天然保留，无需重装。
3. syscall：删除 `sys_read`(fd 0)/`sys_write`(fd 1/2) 裸 fd 特例，改走 `read_fd_current`/`write_fd_current`。
4. 测试：更新 `tests/test_tty_console_input_source.py` 反映 ops 派发 + marker 保留。
5. 验证：默认启动 QEMU headless 回归既有 marker；构建与 clang/clangd 静态检查；`uv run pytest` 跑相关源码测试。

回滚策略：本变更集中在“终端派发结构”。如需回滚，恢复两处裸 fd 特例、移除标准 fd 安装 helper 与 `TTY_OPS` 即可；设备层全局 tty、键盘 IRQ 路径、控制台渲染均未改动，回滚不触及它们。

## Resolved Decisions

- 标准 fd 安装收敛进统一 helper，由 helper 负责构造终端 File 与安装/回滚 fd 0/1/2。exec 不重建 fd 表：`execve_current` 仅调用 `close_on_exec_fds`（`kernel/core/proc/proc.cc` 约 2851 行），只关闭标记 `close_on_exec` 的描述符，其余原样保留。因此标准 tty fd 在进程创建时安装且 `close_on_exec = false`，exec 后天然保留、无需重装；exec 路径不调用该安装 helper。
- 不为本变更新增默认关闭的轻量 smoke。复用既有默认启动 QEMU headless marker 回归（`BIGOS_USER_WRITE_SYSCALL`、`BIGOS_USER_EXEC` 等）覆盖“fd 0/1/2 经 ops 派发且 marker 保留”。
- 标准 fd 的 tty File 统一设为 RDWR（`readable = writable = true`）：fd 0 读、fd 1/2 写在用户语义上区分，但终端读写均合法，统一 RDWR 简化三描述符共享同一 File 的语义。
- 实现暴露的设计修订（终端检测 = `isatty`，而非"裸 fd"）：把终端表达为标准 `vfs::File` 后，fd 0/1/2 不再"无安装 File"，原先 `/bin/sh` 用"`dup(fd)` 成功即视为已重定向 / fd 无 File 即视为交互"的反向 `isatty` 判断会误判为非交互，从而不打印 `$` 提示符、不回显输入（QEMU headless 表现为 `BIGOS_USER_WRITE_SYSCALL` marker 消失）。修订：终端句柄的 `vfs::stat`/`SYS_FSTAT` 报告字符设备（`BIGOS_METADATA_TYPE_CHARDEV` / `S_IFCHR`，经 `terminal::is_tty_file` 识别）；用户态新增基于 `fstat` 的 `isatty()`（无新增 syscall 编号）；`/bin/sh` 的 `is_interactive_session()` 改为 `isatty(0) && isatty(1)`。该路径对 `sh < file`、pipe 等重定向场景正确判定为非交互。涉及 `include/bigos/metadata.h`、`kernel/core/fs/vfs.cc`、`user/libc/include/sys/stat.h`、`user/libc/include/unistd.h`、`user/libc/syscall.c`、`user/sh/sh.c`，并同步更新源码测试 `tests/test_fd_vfs_shell_source.py`、`tests/test_tty_console_input_source.py`。
