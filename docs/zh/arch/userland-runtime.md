# 用户态运行时、libc 与 Shell

BigOS 现在具备一条有界的 freestanding 用户态路径，用户程序源码位于
`user/`。它仍是研究内核的最小用户态，不是完整 POSIX 环境。

## 组件

- `user/crt0/crt0.s`：用户入口 `_start`，消费 `kernel/core/proc/proc.cc` 中
  `copy_exec_args_to_stack` 生成的初始用户栈。
- `user/libc`：有界最小 C 标准库子集，包括 syscall wrapper、errno 翻译、
  字符串/内存函数、基于 `brk` 的 `malloc`/`free`、带 opaque standard streams
  的 fd-backed 极简 stdio、`printf`、`fprintf(stderr, ...)`，以及只读
  `environ`/`getenv`。
- `user/init/init.c`：常驻 PID-1，通过 `fork` + `execve` 启动 `/bin/sh`，
  等待子进程，并在 shell 退出后重新拉起。
- `user/sh/sh.c`：有界交互 shell，支持内建命令、PATH 查找、
  `fork` + `execve` + `wait`、单级管道，以及基本 `<` / `>` 重定向。
- `user/bin/*`：打包进镜像的小型用户程序，例如 `/bin/echo` 和 `/bin/cat`。
- `user/smoke/bin/*`：仅用于验证的 C 探针，只在默认关闭的 `userland_smoke` 路径被选择时构建并打包到 `/bin/smoke/*`。
- `user/smoke/userland_smoke.c`：默认关闭的确定性验证程序，覆盖 crt0、libc
  wrapper、errno、stdout/stderr、smoke C 程序执行、fork/exec/wait、pipe、重定向和 malloc。

## crt0 栈契约

内核进入用户 ELF 镜像时，`rsp` 指向如下布局：

```text
rsp -> argc
       argv[0]
       ...
       argv[argc - 1]
       NULL
       envp[0]
       ...
       NULL
       参数和环境变量字符串
```

`_start` 将 `argc` 放入 `rdi`、`argv` 放入 `rsi`、`envp` 放入 `rdx`，按
System V x86_64 调用边界对齐栈，调用 `main(argc, argv, envp)`，并用 `main`
返回值经 `SYS_EXIT` 退出。它不会返回到未定义地址。

## SYS_EXECVE ABI

`SYS_EXECVE` 以 append-only 方式追加为 syscall 号 `27`。

```text
rax = SYS_EXECVE
rdi = const char *path
rsi = char *const argv[]
rdx = char *const envp[]
```

成功时替换当前进程镜像并进入新程序，不返回。失败时返回负 errno，例如
`-ENOENT`、`-EACCES`、`-ENOEXEC`、`-E2BIG`、`-EFAULT` 或 `-ENOMEM`。内核在
读取目标 ELF 前，会通过 VMA-backed 用户缓冲校验和有界 `EXEC_MAX_*` 限制复制
`path`、`argv` 与 `envp`。

用户 libc 镜像头 `user/libc/include/sys_nr.h` 与
`user/libc/include/errno.h` 刻意不 include C++ 内核头。
`tests/test_syscall_entry_source.py` 会断言它们的数值与
`include/bigos/syscall.h`、`include/bigos/errno.h` 保持一致。

## Shell 边界

Shell 有意保持很小：

- 内建命令：`exit` 和 `echo`。
- Prompt：只在 stdin 与 stdout 仍连接到默认 console fast path 时显示确定性的 `$ `。
- 输入反馈：printable character、newline 和 backspace 由 `read(0, ...)` 返回后的非中断 shell consumer 回显。
- 命令查找：包含 `/` 的路径直接执行；其他命令按 `PATH` 尝试，默认回退 `/bin`。
- 执行方式：外部命令通过 `fork` + `execve` + `wait` 运行。
- 管道：支持一个 `a | b` 单级管道。
- 重定向：每条命令最多一个输入 `< file` 和一个输出 `> file`。
- 输出：builtin、子进程 stdout 和确定性的可恢复错误都使用现有 fd/syscall 路径；未重定向时，默认 fd `1` 与 fd `2` 会到达可见 console。
- 容量：行长、参数数量、PATH 候选数量与路径长度都在 `user/sh/sh.c` 中有固定上限。

本阶段不实现作业控制、后台进程、glob、变量展开、shell 脚本、子 shell、终端进程组、
termios、完整 FILE API、动态链接或完整 POSIX libc。

## 最小 libc 子集

用户态 libc 为简单静态 C 程序暴露有文档边界的有界子集：

- 头文件：`stdio.h`、`stdlib.h`、`string.h`、`errno.h`、`unistd.h`、
  `fcntl.h`、`sys/types.h`、`sys/wait.h`，以及兼容用 umbrella 头
  `libc.h`。
- 类型与常量：`size_t`、`ssize_t`、`off_t`、`pid_t`、`NULL`、已实现的
  open flags、seek 常量、`WAIT_ANY`，以及与 `include/bigos/errno.h` 保持一致
  的 errno 数值。
- Syscall wrapper：内核负 errno 返回会翻译为用户态正 `errno`，并返回 `-1` 或
  接口文档化的失败哨兵；成功 wrapper 不会清零或改写既有 `errno`。
- 字符串与内存：子集包含已实现的有界例程，例如 `strlen`、`strcmp`、
  `strncmp`、`memcpy`、`memset` 和 overlap-safe `memmove`。NULL 指针输入仍遵循
  普通 C 前置条件，BigOS 不额外承诺 hosted 安全检查。
- 堆：`malloc` 返回 16 字节对齐的可写内存；有界失败时返回 `NULL`，且不破坏
  既有块。`free(NULL)` 无副作用。分配器不承诺线程安全、完整 coalescing、
  `realloc` 或 hosted allocator 行为。
- Stdio：`stdin`、`stdout`、`stderr` 只是 fd `0`、`1`、`2` 的 opaque handle。
  `putchar`、`puts`、`printf` 和 `fprintf(stderr, ...)` 基于 fd/write，支持
  `%s`、`%d`、`%x`、`%c` 与 `%%`；不提供 `fopen`、`fclose`、完整 buffering、
  locale、浮点格式化、宽字符或 hosted `FILE` 语义。
- 环境：`envp`、`environ` 与 `getenv` 只读。本阶段不实现 `setenv`、`putenv`
  或 `unsetenv`。

## 简单 C 程序基线

简单 C 程序基线将简单静态 C 程序作为用户可见兼容基线，但仍保持在现有 freestanding
runtime 边界内：

- 入口：`_start` 使用现有用户栈布局调用 `main(argc, argv, envp)`，并将
  `main` 返回值传给 `SYS_EXIT`。
- Wrapper：libc syscall wrapper 将内核负返回值翻译为正的 `errno`，并返回
  `-1` 或接口文档化的失败哨兵。
- 输出：程序使用 fd-based `write`、`putchar`、`puts` 或极简 `printf`；stdout
  是 fd `1`，确定性错误可以写到 fd `2`。
- 环境：`envp`、`environ` 和 `getenv` 只读。若没有提供环境变量，程序必须确定性报告空边界。
- Smoke-only 探针：`/bin/smoke/args`、`/bin/smoke/env`、`/bin/smoke/out`、
  `/bin/smoke/errno`、`/bin/smoke/exit` 和 `/bin/smoke/libc_subset` 在启用
  `userland_smoke` 时分别覆盖参数传递、环境报告、stdout/stderr、wrapper 失败加
  `errno`、请求的退出状态、细粒度 libc 头文件、`fprintf(stderr, ...)`、
  字符串/内存边界和有界堆行为。

该基线不新增 kernel syscall、不修改 `int 0x80` 寄存器 ABI、不改变 boot 或磁盘布局、
不引入动态链接，也不声称提供 hosted libc 或完整 POSIX shell 行为。

## 构建与打包

`xmake.lua` 使用交叉工具链，把用户 C 程序与 `user/crt0`、`user/libc`、
`user/link.lds` 静态链接为 freestanding ELF64 `ET_EXEC` 镜像。启动镜像会打包：

- `/boot/user/init.elf`：默认常驻 C init 或选中的 smoke 程序。
- `/bin/sh`：交互 shell。
- `/bin/echo` 和 `/bin/cat`：正常打包用户命令。
- `/bin/smoke/*` 探针：仅用于显式 `userland_smoke` 验证镜像。

镜像布局仍沿用现有 Legacy BIOS / MBR / exFAT 路径；阶段 19 及后续用户态阶段只在
`/boot/user` 与 `/bin` 下新增文件，不引入 UEFI、AHCI、NVMe、virtio 或新的文件系统后端。
每个用户程序都保持为静态 freestanding ELF64 `ET_EXEC`，并受共享 64 KiB user-ELF 上限约束。

## 验证

默认启动进入 PID-1 init，再进入 `/bin/sh`；QEMU headless 默认 marker 为
`BIGOS_USER_EXEC`。默认关闭的 `userland_smoke` 路径可这样选择：

```bash
xmake f --userland_smoke=y
uv run python tools/boot_debug.py run --emulator qemu --display none --expect-serial-marker BIGOS_USERLAND_PASSED
```

`BIGOS_USERLAND_PASSED` 验证非交互运行时路径。简单 C 程序基线增加面向 smoke-only
C 探针的行为断言：smoke 会观察它们的 stdout/stderr，验证参数和环境报告，验证失败
wrapper 的 `errno` 翻译以及成功路径不改写 `errno`，观察请求的退出码探针，检查有界
libc subset 探针，并通过 `/bin/sh` 运行探针以确认 shell 在外部程序非零退出后继续运行。
交互控制台可用性还保留 default-init headless marker 断言（`BIGOS_USER_EXEC`），并增加
可选的手工或 emulator-input 检查，用于观察文本 console 上的 prompt、输入回显、
backspace feedback 和命令输出。若本地 display、ROM、keyboard input 或 injection 能力不可用，
需要将交互部分记录为 skipped 或 blocked，并写明替代 source/build/headless 检查和剩余
console-usability 风险。
