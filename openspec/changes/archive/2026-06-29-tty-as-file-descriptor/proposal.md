## Why

当前内核里 tty 是一个全局单例（`g_input` 输入环 + `g_input_wait` 等待队列），从未表达为 `vfs::File`；标准描述符 fd 0/1/2 也没有 File 对象，进程创建时 `init_fd_table` 只把 `fd_table` 置空。读写终端依赖两处“裸 fd 特例”分流：`sys_read` 对 `fd==0 且无装载 File` 直接读全局 tty，`sys_write` 对 `fd==1/2 且无装载 File` 直接写默认控制台。这导致描述符派发不统一：常规文件/pipe/socket 走 `file->ops`，而终端走特例分支。后续多路复用与统一 readiness 派发都希望“对每个 fd 一致地调 `ops`”，因此现在把终端表达为标准 `vfs::File`，消除裸 fd 特例。

## What Changes

- 采用 Linux 风格的“设备/句柄两层”模型：保留全局 tty 作为“设备层”（`g_input`/`g_input_wait` 仍为静态、长期存在），新增 per-open 的 `vfs::File` 句柄层，`File.private_data` 指向全局 tty。
- 新增终端文件操作表 `TTY_OPS`（`read`/`write`/`close`，并预留可选 `poll` 槽位的接入点），其 `read` 复用既有 `read_char_blocking`/`read_raw_available_blocking`，`write` 复用既有 `default_terminal_write`，`close` 对设备层为 no-op。
- 进程创建时安装标准描述符：建立一个可读可写的 tty `vfs::File`，安装到 fd 0/1/2（三描述符共享同一 File，`ref_count` 相应增加），使标准输入/输出/错误成为真正的 fd 表项。
- 收编 `sys_read`(fd 0) 与 `sys_write`(fd 1/2) 的裸 fd 特例：终端读写改为经 fd 表 `file->ops` 派发；headless 验证依赖的 COM1 marker（`BIGOS_USER_WRITE_SYSCALL` 等）原样保留在终端 write 路径内。
- tty `vfs::File` 走标准 `retain`/`release` 引用计数（`ref_count==0` 释放 File 结构本身），与 pipe/socket 句柄语义一致；不对单例 File 开“禁止释放”特例。`fork` 复制 fd 表时按既有路径对 tty File `retain`，`close`/`dup`/`dup2` 行为与普通 File 一致。
- 更新钉住裸 fd 特例的源码测试，使其反映“终端经 File ops 派发 + marker 保留”的新结构。
- 非目标：不实现完整 POSIX tty 语义（termios 全集、行规程、SIGTTIN/SIGTTOU、控制终端会话语义之外的扩展）、不引入多路复用 syscall（M13.3）、不引入非阻塞标志（M13.2）、不改变键盘 IRQ 输入路径与控制台渲染、不改变 serial marker 的文本与时机。

## Capabilities

### New Capabilities
- `tty-as-file-descriptor`: 把全局终端表达为标准 `vfs::File` 句柄（设备/句柄分层），将 fd 0/1/2 安装为终端 File，并把终端读写从裸 fd 特例收编到统一的 `file->ops` 派发，同时保留既有 headless 验证 marker 与终端阻塞/输入行为。

### Modified Capabilities
<!-- 终端读写路径由裸 fd 特例改为经 File ops 派发，属于内核内派发结构变化；以新 capability 表达，不在此修改既有 capability 的对外 spec 级 requirement。-->

## Impact

- 受影响内核子系统：terminal/tty（`kernel/core/terminal/tty.cc`、`include/bigos/tty.h`）、VFS（`include/bigos/fs/vfs.h`、`kernel/core/fs/vfs.cc`，新增 `TTY_OPS` 与终端 File 构造）、进程 fd 表（`kernel/core/proc/proc.cc` 的 `init_fd_table`/进程创建/`fork` 复制/`close_on_exec`）、syscall（`kernel/core/syscall/syscall.cc` 的 `sys_read`/`sys_write` 特例收编）。
- 触点清单（基于现状）：`init_fd_table`（[proc.cc:1675](kernel/core/proc/proc.cc)）在约 5 处进程创建路径被调用，需决定标准 fd 安装时机；`sys_write` fd 1/2 特例（[syscall.cc:139](kernel/core/syscall/syscall.cc)）与 `sys_read` fd 0 特例（[syscall.cc:225](kernel/core/syscall/syscall.cc)）收编；`fork` 复制 fd 表的 `retain` 路径（[proc.cc:2038](kernel/core/proc/proc.cc)）；源码测试 `tests/test_tty_console_input_source.py`（断言 `(__fd == 1 || __fd == 2)` 等）需同步更新。
- ABI/接口影响：`FileOperations` 仅按既有“只追加不重排”约定使用既有槽位（`read`/`write`/`close`）；不新增 syscall 编号、不改 syscall ABI；不改动 boot 地址、链接脚本、IDT/syscall 向量、页表布局或磁盘布局。
- 验证：复用既有默认启动 headless 验证（`BIGOS_USER_EXEC`/`BIGOS_USER_WRITE_SYSCALL` 等 COM1 marker）确认终端读写行为不变；通过 QEMU headless 路径回归。本变更不引入新的默认关闭 smoke 开关（如确有需要可加一个轻量验证开关）。
- 架构/内存/仿真器/工具链假设：x86_64-only、UEFI 默认 backend；freestanding-safe，无 hosted libc/异常/RTTI；沿用 `x86_64-elf-gcc` 与 xmake；全局 tty 设备层保持静态、不参与堆释放，句柄层经 `kmalloc`/`release` 管理。
- 与 `fd-readiness-model`（M13.1）的关系：本变更先行落地后，终端 readiness 可直接经 `TTY_OPS` 的 `poll` op 统一派发，M13.1 不再需要 tty 裸 fd 特例桥接。
