# 用户态运行时、libc 与 Shell

BigOS 现在具备一条有界的 freestanding 用户态路径，用户程序源码位于
`user/`。它仍是研究内核的最小用户态，不是完整 POSIX 环境。

## 组件

- `user/crt0/crt0.s`：用户入口 `_start`，消费 `kernel/core/proc/proc.cc` 中
  `copy_exec_args_to_stack` 生成的初始用户栈。
- `user/libc`：最小 C 运行时支持，包括 syscall wrapper、errno 翻译、字符串/内存
  函数、基于 `brk` 的 `malloc`/`free`、极简 stdio/printf，以及只读
  `environ`/`getenv`。
- `user/init/init.c`：常驻 PID-1，通过 `fork` + `execve` 启动 `/bin/sh`，
  等待子进程，并在 shell 退出后重新拉起。
- `user/sh/sh.c`：有界交互 shell，支持内建命令、PATH 查找、
  `fork` + `execve` + `wait`、单级管道，以及基本 `<` / `>` 重定向。
- `user/bin/*`：打包进镜像的小型用户程序，例如 `/bin/echo`。
- `user/smoke/userland_smoke.c`：默认关闭的确定性验证程序，覆盖 crt0、libc
  wrapper、errno、fork/exec/wait、pipe、重定向和 malloc。

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

## 构建与打包

`xmake.lua` 使用交叉工具链，把用户 C 程序与 `user/crt0`、`user/libc`、
`user/link.lds` 静态链接为 freestanding ELF64 `ET_EXEC` 镜像。启动镜像会打包：

- `/boot/user/init.elf`：默认常驻 C init 或选中的 smoke 程序。
- `/bin/sh`：交互 shell。
- `/bin/echo` 和其他有界测试二进制。

镜像布局仍沿用现有 Legacy BIOS / MBR / exFAT 路径；阶段 19 只在
`/boot/user` 与 `/bin` 下新增文件，不引入 UEFI、AHCI、NVMe、virtio 或新的文件系统后端。

## 验证

默认启动进入 PID-1 init，再进入 `/bin/sh`；QEMU headless 默认 marker 为
`BIGOS_USER_EXEC`。默认关闭的 `userland_smoke` 路径可这样选择：

```bash
xmake f --userland_smoke=y
uv run python tools/boot_debug.py run --emulator qemu --display none --expect-serial-marker BIGOS_USERLAND_PASSED
```

`BIGOS_USERLAND_PASSED` 验证非交互运行时路径。Stage 20 还保留 default-init
headless marker 断言（`BIGOS_USER_EXEC`），并增加可选的手工或 emulator-input 检查，
用于观察文本 console 上的 prompt、输入回显、backspace feedback 和命令输出。若本地
display、ROM、keyboard input 或 injection 能力不可用，需要将交互部分记录为 skipped
或 blocked，并写明替代 source/build/headless 检查和剩余 console-usability 风险。
