## Why

各子系统当前各自定义错误码，值重复、命名分裂：`SYS_EBADF`/`FD_EBADF`、
`SYS_EWOULDBLOCK`/`FD_EWOULDBLOCK`/`WAIT_EWOULDBLOCK`、`SYS_EINVAL`/`WAIT_EINVAL`、
`SYS_EMFILE`/`FD_EMFILE` 等同值常量分散在 `include/bigos/syscall.h` 与
`include/bigos/proc.h`。趁现在仅约 10 个值时收敛到单一来源成本极低；一旦 POSIX
铺开几十个码，重复定义将成为长期维护负担与语义漂移风险。这是 roadmap userland runtime baseline.1，
独立、应先于其他所有阶段完成。

## What Changes

- 新增单一错误码来源头文件 `include/bigos/errno.h`，集中定义内核统一的负错误码
  常量（如 `EBADF`/`EWOULDBLOCK`/`EINVAL`/`EMFILE`/`EFAULT`/`ENOSYS`/`ECHILD`），
  以 POSIX 习惯的 `errno`（正值）与按惯例取负后写入返回寄存器的语义为基准。
- 让 `include/bigos/syscall.h`、`include/bigos/proc.h` 中现有的 `SYS_E*`、
  `FD_E*`、`WAIT_E*` 重复常量统一引用 `bigos/errno.h` 的单一定义。
- 更新所有引用点（`kernel/core/syscall/syscall.cc`、`kernel/core/proc/proc.cc`
  等）改用统一错误码符号，删除按子系统前缀的重复别名。
- 同步更新受影响文档（`docs/en`/`docs/zh` 的 syscall-entry、fd-vfs-shell）与相关
  source-contract 测试中对错误码符号名的断言。
- 纯机械收敛：错误码的**数值不变**，仅符号名收敛与去重，不改变任何运行时行为或
  ABI 返回值。

## Capabilities

### New Capabilities
- `unified-errno`: 定义内核单一错误码来源 `bigos/errno.h` 的契约——错误码命名、
  数值稳定性、按惯例取负写入返回寄存器的约定，以及「禁止子系统再各自定义重复
  错误码常量」的收敛要求。

### Modified Capabilities
<!-- 现有 spec 仅以 `-ENOSYS` 等泛化措辞引用错误码，不固定具体符号名；本次为纯机械
     符号收敛、数值与可观察行为不变，因此不修改现有 capability 的需求。 -->

## Impact

- 受影响子系统：syscall 入口（`kernel/core/syscall`）、进程与 fd/VFS 子系统
  （`kernel/core/proc`、`kernel/core/fs`）。
- 受影响头文件：新增 `include/bigos/errno.h`；修改 `include/bigos/syscall.h`、
  `include/bigos/proc.h`。
- 受影响实现：`kernel/core/syscall/syscall.cc`、`kernel/core/proc/proc.cc` 及其他错误码
  引用点。
- 受影响测试：`tests/test_syscall_entry_source.py`、`tests/test_fd_vfs_shell_source.py`
  及其他断言错误码符号名的 source-contract 测试。
- 受影响文档：`docs/en/arch/syscall-entry.md`、`docs/en/arch/fd-vfs-shell.md` 及其
  `docs/zh` 镜像。
- ABI 影响：无。返回寄存器中的负错误码数值保持不变。

### 假设与非目标

假设：
- 架构：仅 x86_64；不引入架构相关的错误码差异。
- 错误码取负写入返回寄存器（`rax`）的现有惯例不变。
- 工具链/构建：xmake + `x86_64-elf-gcc`，错误码为 freestanding-safe 的整型常量。

非目标：
- 不实现完整 POSIX errno 全集，仅收敛当前已使用的约 10 个值。
- 不引入用户态 libc 的 `errno` 全局变量或 `strerror` 机制。
- 不改变任何错误码数值、不新增/删除任何错误语义、不修改 syscall ABI。
- 不引入按线程的 `errno` 存储或错误码到字符串的运行时映射。
