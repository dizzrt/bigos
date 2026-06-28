# 用户态运行时、libc 与 Shell

BigOS 现在具备一条有界的 freestanding 用户态路径，用户程序源码位于
`user/`。它仍是研究内核的最小用户态，不是完整 POSIX 环境。

## 组件

- `user/crt0/crt0.s`：用户入口 `_start`，消费 `kernel/core/proc/proc.cc` 中
  `copy_exec_args_to_stack` 生成的初始用户栈。
- `user/libc`：有界最小 C 标准库子集，包括 syscall wrapper、errno 翻译、
  cwd wrapper、ASCII/C-locale-style `ctype`、有界 `time.h`/`assert.h`、
  字符串/内存函数、基于 `brk` 的 `malloc`/`free`、带行/全/无缓冲的有界缓冲
  `FILE` 流子集、`printf`、`fprintf`、确定性错误文本，
  以及只读 `environ`/`getenv`。
- `user/init/init.c`：常驻 PID-1，通过 `fork` + `execve` 启动 `/bin/sh`，
  等待子进程，并在 shell 退出后重新拉起。
- `user/sh/sh.c`：有界交互 shell，支持内建命令、cwd-aware PATH 查找、
  `fork` + `execve` + `wait`、单级管道，以及基本 `<` / `>` 重定向。
- `user/bin/*`：打包进镜像的小型用户程序，例如 `/bin/echo`、`/bin/cat`、
  `/bin/ls`、`/bin/mkdir`、`/bin/rm`、`/bin/rename`、`/bin/stat`，以及有界日常工具
  `cp`、`mv`、`tee`、`write`、`append`、`head`、`tail`、`wc`、`grep`、
  `hexdump`、`date`、`kill`、`sleep`、`basename`、`dirname`、`more`、`find` 和 `du`。
- `user/smoke/bin/*`：仅用于验证的 C 探针，只在默认关闭的 `userland_smoke` 路径被选择时构建并打包到 `/bin/smoke/*`。
- `user/smoke/userland_smoke.c`：默认关闭的确定性验证程序，覆盖 crt0、libc
  wrapper、errno、stdout/stderr、smoke C 程序执行、fork/exec/wait、pipe、重定向和 malloc。

## 有界核心工具

默认 `/bin` 命名空间暴露一组 BigOS 有界核心工具。它们是小型 freestanding 静态
用户 ELF，不是 GNU coreutils 或完整 POSIX utilities：

- 文件字节流和文本过滤：`cat`、`tee`、`head`、`tail`、`wc`、`grep`、
  `hexdump` 和 `more`。`grep` 只做普通字节子串匹配；regex flag 等未支持 option
  或 pattern form 会确定性失败。
- 路径、目录和元数据观察：`ls`、`find`、`du`、`stat`、`basename` 和 `dirname`。
- 运行期文件修改：`mkdir`、`rmdir`、`rm`、`touch`、`truncate`、`cp`、`mv`、
  `rename`、`write` 和 `append`。这些工具复用现有 cwd、只读 `/boot` 和可写 `/rw`
  契约，不新增 symlink、hard link、权限、atomic replacement、journaling 或跨重启持久性承诺。
- 时间/进程辅助和 BigOS 维护：`date`、`sleep`、`kill` 和 `mkfs_bigfs`。
- Shell 组合消费者：这些工具继承 fd `0`/`1`/`2`，并通过 `/bin/sh` 支持的 PATH 查找、
  `<`/`>` 重定向和单级 pipe 进行验证。

网络诊断命令刻意不属于本批默认核心工具集合；用户可见 socket/network 体验留给单独的有界网络变更。

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
System V x86_64 调用边界对齐栈，调用 `main(argc, argv, envp)`，并把 `main`
返回值传给 libc `exit()`，由它在发出 `SYS_EXIT` 前刷新缓冲 stdio 流。它不会返回到未定义地址。

## 当前目录

每个用户进程拥有一个有界 cwd 字符串，默认初始化为 `/`。`fork` 会独立复制 cwd，
`execve` 会保留 cwd，`chdir` 只有在内核解析目标并确认它是目录后才提交新 cwd。
Path-taking wrapper 会把绝对路径或相对路径原样交给内核；libc 与 shell 不实现单独的 namespace、
symlink traversal、`chroot` 或 `realpath`。

Resolver 支持普通 component、重复 separator、POSIX-style `.` 与 `..`，并让 root 的父目录保持 root。
`getcwd` 把 NUL 结尾 cwd 复制到调用者缓冲区；有效缓冲区过小时报告 `ERANGE`。

## SYS_EXECVE ABI

`SYS_EXECVE` 以 append-only 方式追加为 syscall 号 `27`。

```text
rax = SYS_EXECVE
rdi = const char *path
rsi = char *const argv[]
rdx = char *const envp[]
```

成功时替换当前进程镜像并进入新程序，不返回。失败时返回负 errno，例如
`-ENOENT`、`-EACCES`、`-ENOEXEC`、`-E2BIG`、`-EFAULT`、`-ENOMEM`、
`-EWOULDBLOCK` 或 `-EIO`。内核在读取目标 ELF 前，会通过 VMA-backed 用户缓冲校验和有界
`EXEC_MAX_*` 限制复制 `path`、`argv` 与 `envp`。

用户 libc 镜像头 `user/libc/include/sys_nr.h` 与
`user/libc/include/errno.h` 刻意不 include C++ 内核头。
`tests/test_syscall_entry_source.py` 会断言它们的数值与
`include/bigos/syscall.h`、`include/bigos/errno.h` 保持一致。

## Shell 边界

Shell 有意保持很小：

- 内建命令：`exit`、`echo`、`cd`、`pwd`、`status`、`sync`、`help`、`env`、`clear`、
  `true` 和 `false`。`cd` 与 `pwd` 在 shell 进程内执行，因此 shell 能直接改变和观察 cwd 状态。
- Prompt：只在 stdin 与 stdout 仍连接到默认 console fast path 时显示确定性的 `$ `。
- 输入反馈：printable character、newline 和 backspace 由 `read(0, ...)` 返回后的非中断 shell consumer 回显。
- 命令查找：包含 `/` 的路径直接执行；其他命令按 `PATH` 尝试，默认回退 `/bin`。
- 执行方式：外部命令通过 `fork` + `execve` + `wait` 运行。
- 管道：支持一个 `a | b` 单级管道。
- 前台控制：运行外部命令或单级管道前，shell 会把子进程或管道放入有界 process
  group，把单一默认终端 foreground group 绑定到该组，等待完成后恢复 shell 自身
  process group 和 canonical terminal input mode。这只是 BigOS 的 foreground-command 子集。
- 重定向：每条命令最多一个输入 `< file` 和一个输出 `> file`。
- cwd 行为：包含 `/` 的相对命令路径、重定向路径、`pwd` 等 shell 内建以及外部工具都使用内核 cwd 契约。
- 输出：builtin、子进程 stdout 和确定性的可恢复错误都使用现有 fd/syscall 路径；未重定向时，默认 fd `1` 与 fd `2` 会到达可见 console。
- 容量：行长、参数数量、PATH 候选数量与路径长度都在 `user/sh/sh.c` 中有固定上限。

本阶段不实现完整作业控制、后台进程、`fg`/`bg`、job table、glob、变量展开、shell
脚本、子 shell、`termios`、多终端、完整目录 API、symlink、持久完整可写文件系统、
广泛 file-backed `mmap`、完整 FILE API、动态链接、SMP 或完整 POSIX libc。

## 最小 libc 子集

用户态 libc 为简单静态 C 程序暴露有文档边界的有界子集：

- 头文件：`assert.h`、`ctype.h`、`stdio.h`、`stdlib.h`、`string.h`、
  `errno.h`、`time.h`、`unistd.h`、`fcntl.h`、`sys/types.h`、`sys/wait.h`、
  `sys/stat.h`、`bigos_dirent.h`、`bigos_terminal.h`，以及兼容用 umbrella 头 `libc.h`。Raw syscall
  primitive 需要显式包含 `bigos_syscall.h`，不会从普通 umbrella 头导出。标准
  freestanding 头（`stddef.h`、`stdint.h`、`limits.h`、`stdbool.h`、`stdarg.h`）
  在 `-ffreestanding` 下直接复用交叉工具链版本；本仓库不提供副本。`sys/types.h`
  的 `size_t`/`NULL` 转引工具链 `<stddef.h>`，仅定义 BigOS 自有类型
  （`ssize_t`/`off_t`/`mode_t`/`pid_t`）。
- 类型与常量：`size_t`、`ssize_t`、`off_t`、`pid_t`、`NULL`、已实现的
  open flags、有界 fd-control 常量（`F_GETFD`、`F_SETFD`、`F_DUPFD`、
  `FD_CLOEXEC`）、access mode bits、seek 常量、`WAIT_ANY`、`WNOHANG`，以及与
  `include/bigos/errno.h` 保持一致的 errno 数值，包括用于 `getcwd` 小缓冲区的
  `ERANGE`。
- 文件元数据与目录 helper：`struct stat`、`S_ISDIR`、`S_ISREG` 和
  `struct bigos_dirent` 只描述当前有界文件/目录子集。`bigos_readdir` 读取有界批次；
  它仍是 BigOS-specific 批量 helper；`DIR`、`struct dirent`、`opendir`、`readdir`
  和 `closedir` 提供有界 `DIR*` 风格 wrapper。这些接口不是完整 POSIX 目录遍历，
  不承诺排序、跨调用快照、`telldir`、`seekdir`、`rewinddir`、symlink、目录 fd 语义或
  持久完整文件系统语义。
- Syscall wrapper：内核负 errno 返回会翻译为用户态正 `errno`，并返回 `-1` 或
  接口文档化的失败哨兵；成功 wrapper 不会清零或改写既有 `errno`。`waitpid`、
  `fcntl`、`access`、`stat`、`fstat`、`truncate` 和 `ftruncate` 等 POSIX-like
  名称都是 BigOS 有界子集，不表示完整 POSIX 行为。
- Terminal mode wrapper：`bigos_tcgetmode` 和 `bigos_tcsetmode` 暴露单一默认
  terminal 的 BigOS-specific canonical/raw mode object。它们不声明 POSIX
  `tcgetattr`/`tcsetattr`、完整 `termios`、baud rate、`VMIN/VTIME`、
  pseudo-terminal 或 terminal database 行为。
- BigOS-specific helper：`wait_status`、`bigos_readdir`、`brk_raw`、
  `mmap_anon`、`time_now` 和 `get_tick` 是 public bounded ABI helper，因为当前
  shell、smoke、libc 或打包用户程序路径会使用它们。Raw `syscall0` 到 `syscall6`
  只作为 libc 内部或显式包含 `bigos_syscall.h` 的低层 BigOS ABI helper 保留；
  它们不翻译 `errno`，也不是 POSIX `syscall(2)` 兼容。
- `ctype`、time 与 assert：`ctype.h` 只提供确定性的 ASCII/C-locale 分类
  （`isalnum`、`isalpha`、`isblank`、`iscntrl`、`isdigit`、`isgraph`、`islower`、
  `isprint`、`ispunct`、`isspace`、`isupper`、`isxdigit`）和
  `toupper`/`tolower`。`time.h` 暴露由 BigOS 有界 time primitive 支撑的秒级
  `time()`。`assert.h` 支持 `NDEBUG`；启用的断言失败会向 stderr 输出确定性诊断，
  并通过用户态 libc exit 路径终止。
- 字符串与内存：子集包含已实现的有界例程，例如 `strlen`、`strcmp`、
  `strncmp`、`strcpy`、`strncpy`、`strcat`、`strncat`、`memcpy`、`memcmp`、
  `memset` 和 overlap-safe `memmove`，以及无隐藏状态 search helper：`strchr`、
  `strrchr`、`strstr`、`strspn`、`strcspn`、`strpbrk` 和 `memchr`，还有显式可重入
  tokenizer `strtok_r`（调用方提供 `saveptr`）。它不暴露隐藏全局态的 `strtok`。
  NULL 指针输入仍遵循普通 C 前置条件，BigOS 不额外承诺 hosted 安全检查。
- Stdlib 与堆：`strtol`、`strtoul`、`strtoll` 和 `strtoull` 支持有界整数解析、
  base 处理、`endptr`、no-digit 行为和 `ERANGE`；`atoi` 是十进制便利 wrapper。
  `abs`/`labs` 给出整数绝对值，`qsort`/`bsearch` 使用调用方比较器且无 locale。
  `malloc`、`calloc` 和 `realloc` 使用有界 brk allocator；`calloc` 检查乘法溢出并
  清零内存，`realloc` 失败时保留原块，`free(NULL)` 无副作用。分配器不承诺线程
  安全、完整 coalescing 或 hosted allocator 行为。
- Stdio：`stdin`、`stdout`、`stderr` 是绑定 fd `0`、`1`、`2` 的静态缓冲 `FILE`
  流（`stderr` 无缓冲、`stdout` 行缓冲、`stdin` 读缓冲）。libc 暴露有界缓冲
  `FILE` 流子集：`fopen`/`freopen`/`fclose`、缓冲 `fread`/`fwrite`、`fgetc`/
  `getc`/`fgets`/`fputc`/`putc`/`fputs`/单字节 `ungetc`、缓冲控制
  `setvbuf`/`setbuf`（`_IOFBF`/`_IOLBF`/`_IONBF`）、流状态
  `fflush`/`feof`/`ferror`/`clearerr`/`fileno`，以及有界字节定位
  `fseek`/`ftell`/`rewind`。`fopen` 模式映射到已实现的 `open` flags（`"r"`/`"w"`/
  `"a"` 及其 `+`/`b` 变体）；文本与二进制行为一致（不做换行转换），追加用 `lseek`
  模拟（内核无 `O_APPEND`）。`putchar`、`puts`、`printf`、`fprintf` 经流缓冲与共享
  formatter，`snprintf` 对调用方缓冲复用同一 formatter。formatter 支持既有最小
  格式、简单宽度、`%u`、`%p`、`%ld`、`%lu` 和 `%zu`。libc exit 路径刷新所有可刷新
  写流，使缓冲输出即使不显式 `fclose` 也能落盘。不提供 `scanf` 家族、宽流、
  precision、locale、浮点格式化、宽字符、`tmpfile`/`fmemopen`、完整 `fpos_t` 定位，
  或超出本有界子集的其他 hosted `FILE` 语义。
- 环境：`envp`、`environ` 与 `getenv` 只读。本阶段不实现 `setenv`、`putenv`
  或 `unsetenv`。

## 简单 C 程序基线

简单 C 程序基线将简单静态 C 程序作为用户可见兼容基线，但仍保持在现有 freestanding
runtime 边界内：

- 入口：`_start` 使用现有用户栈布局调用 `main(argc, argv, envp)`，并将
  `main` 返回值传给 libc `exit()`，由它在发出 `SYS_EXIT` 前刷新缓冲 stdio 流。
- Wrapper：libc syscall wrapper 将内核负返回值翻译为正的 `errno`，并返回
  `-1` 或接口文档化的失败哨兵。
- 输出：程序使用 fd-based `write`、`putchar`、`puts`、有界 `printf`/`fprintf`
  或 `snprintf`；stdout 是 fd `1`，确定性错误可以写到 fd `2`。
- 环境：`envp`、`environ` 和 `getenv` 只读。若没有提供环境变量，程序必须确定性报告空边界。
- Smoke-only 探针：`/bin/smoke/args`、`/bin/smoke/env`、`/bin/smoke/out`、
  `/bin/smoke/errno`、`/bin/smoke/exit` 和 `/bin/smoke/libc_subset` 在启用
  `userland_smoke` 时分别覆盖参数传递、环境报告、stdout/stderr、wrapper 失败加
  `errno`、请求的退出状态、细粒度 libc 头文件、ASCII/C-locale `ctype`、有界
  `time.h`、启用的 `assert`、`strtoul`、无隐藏状态 search helper、
  `fprintf(stderr, ...)`、`snprintf`/formatter 行为、字符串/内存边界、
  `strtol`/`atoi`、`calloc`/`realloc`、`DIR*` wrapper 和有界堆行为。

该基线不新增 kernel syscall、不修改 `int 0x80` 寄存器 ABI、不改变 boot 或磁盘布局、
不引入动态链接，也不声称提供 hosted libc 或完整 POSIX shell 行为。

## 构建与打包

`xmake.lua` 使用交叉工具链，把用户 C 程序与 `user/crt0`、`user/libc`、
`user/link.lds` 静态链接为 freestanding ELF64 `ET_EXEC` 镜像。启动镜像会打包：

- `/boot/user/init.elf`：默认常驻 C init 或选中的 smoke 程序。
- `/bin/sh`：交互 shell。
- `/bin/echo`、`/bin/cat`、`/bin/ls`、`/bin/mkdir`、`/bin/rm`、`/bin/rmdir`、
  `/bin/rename`、`/bin/stat`、`/bin/touch`、`/bin/truncate`、`/bin/mkfs_bigfs`，
  以及有界日常工具 `cp`、`mv`、`tee`、`write`、`append`、`head`、`tail`、`wc`、
  `grep`、`hexdump`、`date`、`kill`、`sleep`、`basename`、`dirname`、`more`、`find`
  和 `du`：正常打包用户命令。`pwd` 是 shell 内建，不再作为默认外部程序打包。
- `/bin/smoke/*` 探针：仅用于显式 `userland_smoke` 验证镜像。

镜像布局仍沿用现有 Legacy BIOS / MBR / exFAT 路径；当前有界用户态基线只在
`/boot/user` 与 `/bin` 下新增文件，不引入 UEFI、AHCI、NVMe、virtio 或新的文件系统后端。
每个用户程序都保持为静态 freestanding ELF64 `ET_EXEC`，并受共享 64 KiB user-ELF 上限约束。

## 验证

默认启动进入 PID-1 init，再进入 `/bin/sh`；QEMU headless 默认 marker 为
`BIGOS_USER_EXEC`。默认关闭的 `userland_smoke` 路径可这样选择：

```bash
xmake f --userland_smoke=y
uv run python -m tools.bigosdev run --emulator qemu --display none --expect-serial-marker BIGOS_USERLAND_PASSED
```

`BIGOS_USERLAND_PASSED` 验证非交互运行时路径。简单 C 程序基线增加面向 smoke-only
C 探针的行为断言：smoke 会观察它们的 stdout/stderr，验证参数和环境报告，验证失败
wrapper 的 `errno` 翻译以及成功路径不改写 `errno`，观察请求的退出码探针，检查 cwd-relative
open/stat/`..`、fork 继承、通过 cwd-relative 外部程序执行观察 exec 保留、shell `cd`/`pwd`，并通过有界
libc subset 探针覆盖 public headers、ctype、time、assert、无符号转换、无隐藏状态 search
helper、formatter 行为、错误文本、目录 wrapper 和失败路径。它也会通过 `/bin/sh`
运行探针以确认 shell 在外部程序非零退出后继续运行。
Shell 验证还会用代表性有界核心工具覆盖 `/boot` 只读输入、`/rw` 修改、cwd-relative
路径、输出重定向、输入/输出 fd 继承、单级 pipe、固定缓冲文本过滤、unsupported tool
option、多输入部分失败和只读目标失败。
交互控制台可用性还保留 default-init headless marker 断言（`BIGOS_USER_EXEC`），并增加
可选的手工或 emulator-input 检查，用于观察文本 console 上的 prompt、输入回显、
backspace feedback 和命令输出。若本地 display、ROM、keyboard input 或 injection 能力不可用，
需要将交互部分记录为 skipped 或 blocked，并写明替代 source/build/headless 检查和剩余
console-usability 风险。

## 有界动态链接（默认关闭）

默认关闭的有界动态链接路径在静态基线之上新增了最小的"动态链接 + 共享库"机制。
它由一个构建开关守卫，不改变默认静态启动：开关关闭时不构建或打包任何解释器、
共享库或动态可执行程序，装载器继续对 `PT_INTERP`/`ET_DYN` 镜像确定性拒绝。

开关启用时，内核 ELF 装载器接受含恰好一个 `PT_INTERP` 的有界 `ET_DYN` 主可执行
镜像。内核把主镜像和 `PT_INTERP` 指定的用户态解释器加载到先前预留运行时 gap 内
确定性、不重叠的基址，在既有 `argc`/`argv`/`envp` 初始栈之后追加有界 auxiliary
vector（`AT_PHDR`/`AT_PHENT`/`AT_PHNUM`/`AT_ENTRY`/`AT_BASE`/`AT_PAGESZ`/`AT_NULL`），
并以解释器入口（而非主镜像入口）进入 ring3。静态 `ET_EXEC` 路径、其有界限制、
`int 0x80` 寄存器 ABI、syscall number 与 boot/磁盘布局均不变；auxv 是追加式的，
因此既有静态 crt0（只读到 `envp` 的 NULL 终止符）不受影响。

重定位与符号绑定完全在用户态解释器（`ld-bigos.so`）中进行，内核不做重定位。
解释器是 freestanding、位置无关的 `ET_DYN`：先自重定位（仅 `R_X86_64_RELATIVE`，
用 `AT_BASE`），再经 auxv 定位主镜像 program header，把有界数量的 `DT_NEEDED`
共享对象从 `/lib` 加载到有界共享对象映射区，执行 eager（`BIND_NOW` 等价）重定位
子集（`R_X86_64_RELATIVE`/`GLOB_DAT`/`JMP_SLOT`/`64`）并按全局作用域（主镜像优先、
其后加载顺序）绑定符号，最后跳转到主镜像真实入口。两个有界辅助 syscall 让解释器
在共享对象映射区内预留并改写页权限；二者都拒绝任何不在有界动态布局上的进程。

该路径是刻意有界的，**不是**完整 POSIX 动态链接器。它不实现 `dlopen`/`dlsym`/
`dlclose`、`LD_PRELOAD`/`LD_LIBRARY_PATH` 搜索、符号版本、GNU hash、多解释器、
TLS（`PT_TLS`/`*TPOFF*`/`DTPMOD`）、`IFUNC`/`IRELATIVE`、lazy PLT 解析，也不把默认
libc 拆为共享对象。未支持的重定位类型、未解析的非弱符号、缺失的解释器或
`DT_NEEDED` 库以及超界数量都走确定性失败（内核 loader/exec 错误，或有界
`BIGOS_DYNLINK_FAILED` 解释器 marker 加 `SYS_EXIT`）；该路径绝不跳转到未重定位或
未定义地址。默认关闭的验证交付一个示例共享库和一个动态可执行程序，端到端走通整条路径：

```bash
xmake f --dynamic_link_smoke=y
uv run python -m tools.bigosdev run --emulator qemu --display none --expect-serial-marker BIGOS_DYNLINK_PASSED
```

`BIGOS_DYNLINK_PASSED` 确认内核加载了动态可执行程序及其解释器，解释器完成自重定位、
加载了示例共享库、绑定了跨模块的函数与数据符号，且程序以预期结果调用了它们。
