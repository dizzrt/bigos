## Why

到阶段 18（可写文件系统 + 页缓存 + 管道，已完成）为止，内核侧已经具备支撑一个真实用户态所需的全部语义：`fork`（阶段 16）、`execve` 级别的 `exec_current_from_elf_image` 内核内复用入口（见 [proc.cc](kernel/core/proc/proc.cc) 第 1419 行）、信号（阶段 17）、墙钟与 uid/gid（阶段 16.5）、可写 FS、`pipe`/`dup`/`dup2`、`lseek`（阶段 18）。但用户态本身仍是**裸的**：唯一的用户程序是手写汇编 `init`（[user/init/init.s](user/init/init.s)），没有 C 运行时启动代码（crt0）、没有任何 syscall wrapper 库、没有 `/bin/sh`，因此无法用 C 写用户程序、无法交互、无法用 `fork`+`execve` 串联命令或用 `pipe`/重定向组合程序。路线图阶段 19 的目标是在上述全部语义之上补齐**最小可用用户态**：crt0 + 最小 libc + `/bin/sh` + 用户态测试二进制，让内核第一次拥有一个可交互、可运行多个用户程序的外壳。趁内核 ABI（`int 0x80`、寄存器约定、向量/DPL 布局）已稳定、syscall 面已基本齐备时建立用户态运行时，是把这些内核能力首次端到端串起来的自然收尾。

## What Changes

- 新增**用户态 C 运行时启动代码（crt0）**：`_start` 汇编入口，从内核按 ELF 装载约定布置好的用户栈上读取 `argc`/`argv`/`envp`，对齐栈、调用 `main(argc, argv, envp)`，并在 `main` 返回后调用 `exit(ret)`（经 `SYS_EXIT`），绝不返回到未定义地址。crt0 不依赖任何宿主运行时、不做 C++ 全局构造（用户程序为 C，freestanding）。
- 新增**最小用户态 libc**（`user/libc`，独立于内核 `cpp`/`ktl`，纯 freestanding，仅经 `int 0x80` 与内核交互）：
  - syscall wrapper：把现有 `bigos::sys::SyscallNumber`（[syscall.h](include/bigos/syscall.h)）逐一封装为 C 函数（`write`/`read`/`open`/`close`/`exit`/`fork`/`execve`/`wait`/`pipe`/`dup`/`dup2`/`lseek`/`fsync`/`mkdir`/`unlink`/`brk`/`mmap`/`getpid`/`getppid`/`getuid`/`getgid`/`kill`/`sigaction`/`sigprocmask`/`time`/`get_tick` 等），并把内核返回的负 errno 翻译为 `errno` + 返回 `-1` 的 POSIX 风格约定（errno 码复用 [errno.h](include/bigos/errno.h) 单一来源）。
  - 最小字符串/内存：`strlen`/`strcmp`/`strncmp`/`strcpy`/`strncpy`/`memcpy`/`memset`/`memmove` 等。
  - 最小堆分配：基于 `SYS_BRK`（与可选 `SYS_MAP_ANON`）的极简 `malloc`/`free`（bump 或最简自由链表，有界、确定性失败）。
  - 最小 stdio：基于 fd 0/1/2 的 `read`/`write` 封装与 `puts`/`putchar`/最小 `printf`（仅常用 `%s`/`%d`/`%x`/`%c`），不引入缓冲文件流的完整 `FILE` 语义。
- 新增 `/bin/sh` **最小交互式 shell**（C，链接用户 libc）：从 stdin 读一行，解析为命令 + 参数（空白分隔），支持少量内建命令（如 `cd`(若引入)、`exit`、`pwd`(若引入)、`echo`），对外部命令做用户态命令查找（命令名含 `/` 直走路径，否则按 `PATH`/缺省 `/bin` 逐目录尝试）后走 `fork`+`execve`+`wait`；支持单级管道 `a | b`（经 `SYS_PIPE`+`SYS_DUP2`）与基本重定向 `>`/`<`（经 `open`+`SYS_DUP2`）。容量有界（最大行长、最大 argc、最大管道段数、PATH 候选数在 design 固化），所有失败确定性报错而非崩溃。
- 新增**用户程序构建与打包轨道**：把现有仅构建手写汇编 `init.elf` 的 `user-init-elf` target（[xmake.lua](xmake.lua) 第 243 行）扩展/泛化为可编译「用户 C 程序 + crt0 + 用户 libc」并静态链接为 `ET_EXEC` ELF64、打包进磁盘镜像（如 `/bin/sh` 与若干 `/bin/*` 测试二进制）的流程，沿用现有 `x86_64-elf-gcc`/`x86_64-elf-as`/`x86_64-elf-ld` 与 `-nostdlib -static` 约定及 64KiB 体积上限（或在 design 中按需调整上限并说明）。
- 在 `int 0x80` ABI 末尾**仅追加** `SYS_EXECVE`（替换当前进程映像，接受用户态 `path`/`argv`/`envp`，复用既有 `exec_current_from_elf_image` + VFS 读路径；成功不返回、失败返回确定性负 errno）。寄存器 ABI、现有号位、向量布局、DPL 设置、「syscall 不发 EOI」均不变。其余 shell 所需能力（`fork`/`wait`/`pipe`/`dup2`/`open`/可写 FS）已在阶段 16/18 就位，本阶段不新增内核语义，只把内核内已有的 exec 复用入口经一个新 syscall 号暴露给用户态。
- 让默认启动的 PID-1 `init`（[proc.cc](kernel/core/proc/proc.cc) `launch_init`）在加载后成为**常驻 C 版 init**：`fork`+`execve` 启动 `/bin/sh`，并以 `while(1) wait` 循环收割退出子进程（含被过继到 PID-1 的孤儿），`/bin/sh` 退出时重新拉起；init 自身不退出。使正常启动进入并持续维持交互式用户态外壳；保留可回退到原汇编 `init` 行为的边界。
- 在内核进程退出路径补齐**最小孤儿过继到 PID-1 init** 的接线（当前内核未实现此语义，[proc.cc](kernel/core/proc/proc.cc) 第 2087 行注释明示 "does not implement PID-1 restart/adoption"）：非 init 进程退出时把其子进程过继给 init 并对僵尸子进程唤醒 init 的 `wait`，使常驻 init 能真正收割孤儿；纯链表指针改写、复用既有 `parent_pid`/`first_child_pid`/`next_sibling_pid` 字段，不新增数据结构、无分配/IO/锁，不改 `wait_current` 既有遍历语义与 init 自身退出边界。
- 收敛真实用户态链路暴露出的**最小内核修复**，均保持本阶段非破坏边界：长模式启用 EFER.NXE 以匹配 `NO_EXECUTE` 用户页属性；`#PF` handler 对成功物化页返回恢复而非落入默认异常；新增 `can_allocate_in_fault()` 作为 ring3 `#PF` 下的分配安全前置；`fork` 从 CPU iret tail 捕获真实用户 `rsp/ss`；ELF 装载页只映射到目标进程 root，避免污染 active root；scheduler 为用户 kernel-thread 恢复 `Process*`/用户 CR3/TSS `rsp0`；fd 1 控制台快路径仅在未绑定真实文件时生效，避免破坏管道/重定向。
- 新增默认关闭的验证开关（如 `userland_smoke`，定义 `BIGOS_USERLAND_SMOKE`），发射固定 COM1/VGA marker（如 `BIGOS_USERLAND_PASSED`/`_FAILED`），覆盖「crt0 正确传递 argc/argv 并以正确退出码退出」「libc syscall wrapper + errno 翻译正确」「shell `fork`+`execve`+`wait` 运行外部命令」「shell 单级管道与重定向」「最小 `malloc`/`free` 正确」等路径；保留现有 smoke 矩阵不删除。
- **非破坏性**：不改变 `int 0x80` 寄存器 ABI 约定（仅末尾追加 `SYS_EXECVE`）、IDT/向量布局、DPL、页表自映射地址、CR3 切换约定、higher-half/direct-map/`KVMEM_BASE` 布局、boot/磁盘镜像布局、MBR/分区/exFAT 发现契约；不引入 SMP/锁；不改变现有内核子系统语义，仅新增用户态侧产物与一个 append-only syscall 号。

## Capabilities

### New Capabilities
- `user-crt0-runtime`: 用户态 C 运行时启动代码——`_start` 入口按内核 ELF 装载约定消费用户栈上的 `argc`/`argv`/`envp`、对齐栈并调用 `main`、`main` 返回后经 `SYS_EXIT` 退出、绝不返回未定义地址，纯 freestanding、不依赖宿主运行时、不做 C++ 全局构造。
- `user-libc-min`: 最小用户态 libc——经 `int 0x80` 的 syscall wrapper 全集与「负 errno -> `errno`+`-1`」翻译、最小字符串/内存函数、基于 `brk`/`mmap` 的极简有界 `malloc`/`free`、基于 fd 0/1/2 的最小 stdio 与最小 `printf`、以及只读的 `environ`/`getenv`（供 shell `PATH` 查找），全部 freestanding、确定性失败、不引入完整 POSIX libc 语义。
- `user-shell`: `/bin/sh` 最小交互式 shell——读行/解析/内建命令、用户态命令查找（含 `/` 直走路径，否则按 `PATH`/缺省 `/bin` 逐目录尝试）、`fork`+`execve`+`wait` 运行外部命令、单级管道与基本重定向，容量有界、确定性报错、不引入完整 POSIX shell 语法（无作业控制、无变量展开、无通配符、无脚本控制流）。
- `user-program-build`: 用户程序构建与打包——把用户 C 程序与 crt0、用户 libc 静态链接为 `ET_EXEC` ELF64 并打包进磁盘镜像（`/bin/sh` 与测试二进制），沿用现有交叉工具链、`-nostdlib -static`、体积上限与镜像安装路径约定，提供确定性的构建失败语义。

### Modified Capabilities
- `syscall-entry`: `int 0x80` ABI 在末尾追加 `SYS_EXECVE`（接受用户态 `path`/`argv`/`envp`，复用既有内核 exec 复用入口与 VFS 读路径，成功不返回、失败返回确定性负 errno，进入同步块 IO/分配前 MUST 检查调度阻塞守卫）；其余 syscall 号、寄存器约定、向量/DPL 布局与「syscall 不发 EOI」不变。
- `user-space-init`: 默认启动的 PID-1 `init` 升级为常驻 C 版 init：`fork`+`execve` 启动 `/bin/sh`、`while(1) wait` 收割退出子进程与被过继的孤儿、`/bin/sh` 退出时重新拉起、init 自身不退出；并在内核退出路径新增最小孤儿过继到 PID-1 的接线（非 init 进程退出时把子进程过继给 init 并唤醒其 `wait`），使常驻 init 能真正收割孤儿。保留确定性的加载/exec 失败降级边界（沿用现有 `BIGOS_INIT_LOAD_FAILED` -> 统一 panic 路径）。
- `runtime-smoke-validation`: 新增默认关闭的 `userland_smoke`（`BIGOS_USERLAND_SMOKE` -> `BIGOS_USERLAND_PASSED`/`_FAILED`），覆盖 crt0、libc wrapper/errno、shell `fork`+`execve`+`wait`、管道/重定向、`malloc`/`free` 路径；现有 smoke 开关与 marker 不变。

## Impact

- 受影响子系统：新增用户态侧树（`user/libc`、`user/crt0` 或 `user/init` 复用、`user/sh`、`user/bin/*` 测试程序）；`kernel/core/syscall`（[syscall.cc](kernel/core/syscall/syscall.cc) 新增 `SYS_EXECVE` 分支）、[syscall.h](include/bigos/syscall.h)（追加号位）；`kernel/core/proc`（[proc.cc](kernel/core/proc/proc.cc) 暴露 `execve` 路径、`launch_init` 改为常驻 C init（fork+execve shell + wait 收割孤儿，并核查/补齐孤儿过继到 PID-1 的语义）、用户态 `argv`/`envp` 拷贝与边界校验复用现有 `copy_exec_args_to_stack`/VMA 校验）；构建系统 [xmake.lua](xmake.lua)（泛化 `user-init-elf` target 为多用户程序构建与打包、新增 `userland_smoke` 开关）；磁盘镜像打包（`tools/`，新增 `/bin/sh` 等路径）。
- 受影响代码：新增 crt0 汇编/链接脚本、用户 libc 头与实现、`/bin/sh` 源、测试二进制源；[syscall.h](include/bigos/syscall.h) 与 [syscall.cc](kernel/core/syscall/syscall.cc)（`SYS_EXECVE` 与 fd 1 条件化控制台快路径）；[proc.cc](kernel/core/proc/proc.cc)（execve syscall 接线、init->shell、孤儿过继、fork 真实 `rsp/ss`、ELF 映射 root 边界、用户线程上下文恢复接线）；[sched.h](include/bigos/sched.h)/`kernel/core/sched/sched.cc`（`can_allocate_in_fault()` 与 per-thread 用户进程上下文恢复）；[interrupt.cc](kernel/core/irq/interrupt.cc)（可恢复 `#PF` 门控）；[boot.s](kernel/arch/x86/boot/boot.s)（EFER.NXE）；[xmake.lua](xmake.lua)（用户程序构建/打包/开关）；`tools/boot_debug.py` 的 userland marker 期望与 `tests/` 行为断言/源码契约测试（沿用阶段 14.5 行为断言轨道）。
- 构建/验证：`xmake` 新增用户程序 target 与默认关闭 `userland_smoke` 开关；QEMU headless serial-marker smoke（crt0/libc/shell/管道/malloc）；交互式 shell 在图形 QEMU 下手工冒烟；涉及 execve/栈布局/ELF 装载边界时在可用环境下补 Bochs 或 QEMU+Bochs 交叉验证；clang/clangd 辅助静态检查（按内核 freestanding/x86_64 配置，用户态部分另按用户 freestanding 配置）；Python 相关经 `uv run` 执行。
- 假设：x86_64 单核、同步、`int 0x80`/`InterruptFrame` ABI 与向量/DPL 布局不变；阶段 15（按需分页）、15.5（可增长进程/fd 表）、16（fork/COW）、16.5（时间与身份）、17（信号）、18（可写 FS/页缓存/`pipe`/`dup`）均已就位且语义不变；用户程序为 C、freestanding、`-nostdlib -static`、`ET_EXEC`、体积有界，经现有内核 ELF64 装载器加载；用户 libc 仅经 `int 0x80` 与内核交互、不假定任何宿主运行时；磁盘镜像/分区/exFAT 路径布局不变，仅新增 `/bin/*` 文件；Bochs/QEMU 经 `tools/boot_debug.py` 验证。
- 非目标：完整 POSIX libc（locale、宽字符、完整 `stdio`/`FILE` 缓冲、`malloc` 分配器质量优化、信号安全异步分配等）、动态链接/共享库/`dlopen`、完整 POSIX shell 语义（作业控制、`&` 后台、变量/环境变量展开 `$VAR`、通配符 glob、`if`/`for`/`while`/函数等脚本控制流、多级复杂管道与子 shell）、`readline`/行编辑/历史、终端作业控制与 `tcsetpgrp`、`exec*` 全族（仅 `execve`）、环境变量写入语义（`setenv`/`putenv`/`unsetenv`；本阶段仅只读 `getenv` 供 shell `PATH` 查找）、C++ 用户程序与异常/RTTI、网络/socket、SMP，以及任何超出「最小可用用户态」的工具集。这些留给后续阶段。注：shell 的 `PATH` 命令查找已纳入本阶段（见 `user-shell`）。
