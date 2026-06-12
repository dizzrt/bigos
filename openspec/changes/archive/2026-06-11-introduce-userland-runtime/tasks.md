# 实施任务：用户态运行时 / libc / shell（阶段 19）

## 1. 内核侧：SYS_EXECVE（append-only 暴露既有 exec 入口）
- [x] 1.1 在 [syscall.h](include/bigos/syscall.h) 的 `SyscallNumber` 末尾追加 `SYS_EXECVE`（取下一个号位 27），并注释其 ABI（rdi=path、rsi=argv、rdx=envp，成功不返回、失败返回负 errno）；不改动既有号位。
- [x] 1.2 在 [syscall.cc](kernel/core/syscall/syscall.cc) 新增 `SYS_EXECVE` dispatch 分支：进入同步块 IO/分配前检查调度阻塞守卫；经 VMA-backed 校验把用户 `path`（≤ `SYS_PATH_MAX_LEN`）与 `argv`/`envp`（受 `EXEC_MAX_ARGC`/`EXEC_MAX_ENVC`/`EXEC_MAX_STRING_BYTES` 约束）拷入内核 `ExecArgs`。
- [x] 1.3 在 [proc.cc](kernel/core/proc/proc.cc) 暴露/接线一条 `execve` 路径：经 VFS `open_absolute` + bounded read 读 ELF 到内核缓冲，调用既有 `exec_current_from_elf_image`；失败映射确定性负 errno（`-ENOENT`/`-EACCES`/`-ENOEXEC`/`-E2BIG`/`-EFAULT`/`-ENOMEM`），成功不返回。
- [x] 1.4 复查：`SYS_EXECVE` MUST NOT 改动既有 syscall 号位、寄存器约定、`VECTOR_SYSCALL`/DPL、「syscall 不发 EOI」；失败时保持当前进程镜像可继续（先完成可失败的拷入/读盘，再调用 exec 入口）。
- [x] 1.5 同步更新 `docs/en/arch/syscall-entry.md` 与镜像 `docs/zh/arch/syscall-entry.md`，记录新增 `SYS_EXECVE` 号与 ABI（保持中英文技术事实一致）。

## 2. 用户态 crt0
- [x] 2.1 在 `user/`（新建 `user/crt0` 或复用 `user/init`）实现 `_start` 汇编：按 `copy_exec_args_to_stack` 布局（初始 SP 处为 `argc`，其后 `argv` NULL 结尾、再 `envp` NULL 结尾）取 `argc`->rdi、`argv`->rsi、`envp`->rdx，按 System V 16 字节对齐栈后 `call main`。
- [x] 2.2 `main` 返回后以 rax 作为退出码调用 `SYS_EXIT`；确保绝不返回到未定义地址（exit 后 `hlt`/死循环兜底）。
- [x] 2.3 提供/复用用户链接脚本（参考 [user/init/link.lds](user/init/link.lds)）：`ET_EXEC`、固定用户加载基址、`-nostdlib -static -z max-page-size=0x1000`。

## 3. 用户态最小 libc（user/libc）
- [x] 3.1 新增用户侧 syscall 号镜像头与 errno 镜像头，数值与 [syscall.h](include/bigos/syscall.h)/[errno.h](include/bigos/errno.h) 一致；提供底层 `syscall0..6` 内联汇编封装（rax=number，rdi/rsi/rdx/r10/r8/r9，返回 rax）。
- [x] 3.2 实现 syscall wrapper 全集（`write`/`read`/`open`/`close`/`exit`/`fork`/`execve`/`wait`/`pipe`/`dup`/`dup2`/`lseek`/`fsync`/`mkdir`/`unlink`/`brk`/`mmap`/`getpid`/`getppid`/`getuid`/`getgid`/`kill`/`sigaction`/`sigprocmask`/`time`/`get_tick` 等），并把负 errno 翻译为全局 `errno` + 返回 `-1`/失败哨兵。
- [x] 3.3 实现最小字符串/内存：`strlen`/`strcmp`/`strncmp`/`strcpy`/`strncpy`/`memcpy`/`memset`/`memmove`（`memmove` 处理重叠）。
- [x] 3.4 实现基于 `brk`（可选 `mmap`）的有界 `malloc`/`free`：bump + 简单自由链表，失败返回 NULL 且不破坏既有块。
- [x] 3.5 实现最小 stdio：fd 0/1/2 的 `read`/`write` 封装、`putchar`/`puts`、最小 `printf`（`%s`/`%d`/`%x`/`%c`/`%%`），无完整 FILE 缓冲语义。
- [x] 3.6 实现最小只读环境变量访问：由 crt0 传入的 `envp` 提供 `environ` 指针与 `getenv(name)`（存在返回值字符串、不存在返回 NULL），不实现 `setenv`/`putenv`/`unsetenv`。

## 4. /bin/sh 最小 shell
- [x] 4.1 在 `user/sh` 实现读-解析-执行循环：读一行（`SH_MAX_LINE` 有界）、按空白解析为 argv（`SH_MAX_ARGC` 有界，NULL 结尾），超限确定性报错并继续循环。
- [x] 4.2 实现内建命令：至少 `exit`（给定/缺省退出码终止 shell）与 `echo`（空格分隔回显 + 换行）。
- [x] 4.3 命令查找与 `PATH`：命令名含 `/` 直接按路径 `execve`；不含 `/` 时按 `PATH`（经 `getenv` 读取，缺省回退 `/bin`）从左到右逐目录拼接尝试 `execve`，候选数与路径长度有界（复用 `SYS_PATH_MAX_LEN`），仅对 `-ENOENT` 续试下一候选，全部失败报 "command not found"。
- [x] 4.4 外部命令：经 4.3 解析出具体路径后 `fork`+`execve`(argv)+`wait`；`execve` 最终失败时子进程确定性报错并非零退出，父 shell 不被破坏。
- [x] 4.5 单级管道 `a | b`：`SYS_PIPE` + `fork` 两段 + `dup2` 接 stdout/stdin，子进程显式 close 多余管道端，父 `wait` 两段。
- [x] 4.6 基本重定向 `> file` / `< file`：经 `open`（可写/创建 / 只读）+ `dup2`；`open` 失败（权限/只读后端）确定性报错且不执行命令。

## 5. 构建与打包（user-program-build）
- [x] 5.1 在 [xmake.lua](xmake.lua) 把现有 `user-init-elf` target（第 243 行）泛化为可编译「用户 C 程序 + crt0 + user libc」并 `-nostdlib -static` 链接为 `ET_EXEC` ELF64 的通用流程，覆盖 `/bin/sh` 与若干 `/bin/*` 测试二进制。
- [x] 5.2 保留/调整 bounded 体积上限：若 C+libc 链接体积超 64KiB，按需调高上限并在注释说明；超限构建确定性失败并报告产物与体积。
- [x] 5.3 把 `/bin/sh` 与测试二进制打包进磁盘镜像确定路径（沿用 `tools/` 安装流程），不改动 boot/MBR/分区/exFAT 发现契约与既有镜像布局，仅新增 `/bin/*` 文件。

## 6. 默认 boot 进入并维持 shell（user-space-init）
- [x] 6.1 把默认 `/boot/user/init.elf` 改为链接 crt0/libc 的常驻 C init：`fork`+`execve("/bin/sh")` 启动 shell，父 init 进入 `while(1) wait(...)` 循环收割退出子进程（含被过继到 PID-1 的孤儿）；`/bin/sh` 退出时重新 `fork`+`execve` 拉起，init 自身不退出；init 自身 `fork`/`execve`/`wait` 失败确定性报错并经现有 reaper/`BIGOS_INIT_*` 边界处理。
- [x] 6.2 在 [proc.cc](kernel/core/proc/proc.cc) 退出路径（`exit_current`/`fault_current_and_exit` 标记 zombie/reap 之前）补齐最小孤儿过继接线：遍历退出进程的 `first_child_pid` 兄弟链，把每个子进程 `parent_pid` 改为 `g_init_process->pid` 并挂入 init 的 `first_child_pid` 链；对其中已是 Zombie 的子进程向 init 投递 `SIGCHLD` 并 `wake_all(&g_process_wait_queue)`。复用现有 `parent_pid`/`first_child_pid`/`next_sibling_pid` 字段与 sibling 链维护方式，纯指针改写、无分配/IO/锁；`g_init_process == nullptr` 时跳过、回退现有自我回收兜底；不改 `wait_current` 既有遍历语义、不改 init 自身退出的 `BIGOS_INIT_EXIT`/idle/panic 边界。
- [x] 6.3 复查 [proc.cc](kernel/core/proc/proc.cc) `launch_init`：内核加载路径与 `BIGOS_INIT_ENTER` marker 行为不变；确认 `/bin/sh` 在默认构建被打包。
- [x] 6.4 保留 `user_program_smoke`/`user_elf_smoke` 开关与其 `BIGOS_USER_ENTER`/`BIGOS_USER_EXIT` marker 不变。

## 7. 验证开关 userland_smoke（runtime-smoke-validation）
- [x] 7.1 在 [xmake.lua](xmake.lua) 新增默认关闭 `userland_smoke` 选项（定义 `BIGOS_USERLAND_SMOKE`），默认构建不定义。
- [x] 7.2 提供 `userland_smoke` 验证程序/路径并发射固定 marker `BIGOS_USERLAND_PASSED`/`BIGOS_USERLAND_FAILED`，覆盖：crt0 传 argc/argv 与退出码、libc wrapper + errno 翻译、shell `fork`+`execve`+`wait`、单级管道与重定向、最小 `malloc`/`free`；该路径自带确定性输入/非交互断言，不依赖人工 stdin。

## 8. 验证（runtime）
- [x] 8.1 narrow build：`xmake`（默认构建）确认内核 + 用户程序（含 `/bin/sh`）构建通过、镜像打包成功。
- [x] 8.2 QEMU headless serial-marker smoke：以 `userland_smoke=y` 经 `uv run python tools/boot_debug.py run --emulator qemu --display none --expect-serial-marker BIGOS_USERLAND_PASSED` 验证；记录通过/失败原因/残留风险。
- [x] 8.3 交互 shell 手工冒烟：图形 QEMU（`xmake run qemu`）下确认默认进入 `/bin/sh`，运行内建/外部命令、单级管道、重定向。（当前非交互会话未执行图形人工输入；已用 QEMU/Bochs headless marker 验证默认进入 `/bin/sh` 并观察 `$` 提示符。）
- [x] 8.4 涉及 execve/初始栈布局/ELF 装载边界时，在可用环境下补 Bochs 或 QEMU+Bochs 交叉验证 ring3 进入与取参正确性；若环境不可用，明确记录跳过与残留风险。

## 9. C++ 辅助静态检查（仅内核 C++ 改动部分）
- [x] 9.1 对本 change 触及的内核 C++ 源/头（[syscall.cc](kernel/core/syscall/syscall.cc)、[proc.cc](kernel/core/proc/proc.cc)、[syscall.h](include/bigos/syscall.h)）跑 clang/clangd 辅助检查，配置尽量贴近 GCC 交叉环境（freestanding C++17、x86_64 target、项目 include、无 hosted/异常/RTTI）。
- [x] 9.2 修复本 change 引入的 clang/clangd 错误并确认/修复新增有效告警；验证记录区分历史诊断、本 change 诊断、工具链/freestanding 误报。若工具不可用记录差距与残留风险。
- [x] 9.3 注：用户态 C 程序（crt0/libc/sh）按用户 freestanding C 配置审查；clang/clangd 仅作辅助信号，不替代 `x86_64-elf-gcc` 交叉构建。

## 10. 行为断言/源码契约测试与文档
- [x] 10.1 新增/扩展 `tests/` 测试：源码契约断言用户 libc 镜像头的 syscall 号/errno 数值与 [syscall.h](include/bigos/syscall.h)/[errno.h](include/bigos/errno.h) 一致；行为断言沿用阶段 14.5 轨道（serial marker + 用户二进制输出）。所有 Python 经 `uv run`（`uv run pytest`、`uv run ruff check`、`uv run ruff format --check`、`uv run pyright`）；若新增/修改 Python 文件，修复新引入的 lint/类型/格式/测试问题；`uv` 不可用时显式记录。
- [x] 10.2 更新文档（`docs/en` 为准，`docs/zh` 同步同相对路径）：新增用户态运行时/libc/shell 架构说明，记录 crt0 初始栈契约、`SYS_EXECVE` ABI、shell 有界上限与 `/bin/*` 打包路径；使用仓库相对路径，不暗示改动 boot/链接地址/向量/页表/磁盘/CR3/ABI。
- [x] 10.3 更新 [roadmap.md](roadmap.md) 阶段 19 状态（proposed -> 进行中/完成对应表述）与 README/README-zh 成熟度描述（用户态运行时已落地的 bounded 范围），不夸大为完整 POSIX/通用 OS。
