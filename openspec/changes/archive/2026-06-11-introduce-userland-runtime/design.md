## Context

阶段 18 完成后，内核侧支撑真实用户态所需的语义已基本齐备：进程生命周期与安全回收、`fork`/COW（阶段 16）、信号（阶段 17）、墙钟与 uid/gid（阶段 16.5）、可写 FS + 页缓存 + `pipe`/`dup`/`dup2`/`lseek`（阶段 18），以及内核内已存在但尚未经 syscall 暴露的当前进程镜像替换入口 `exec_current_from_elf_image`（[proc.cc](src/kernel/proc/proc.cc) 第 1419 行）。`launch_init`（[proc.cc](src/kernel/proc/proc.cc) 第 1065 行）已经在 normal boot 默认加载 `/boot/user/init.elf` 并进入 ring3，初始用户栈布局由 `copy_exec_args_to_stack`（[proc.cc](src/kernel/proc/proc.cc) 第 829 行）按 `[argc][argv...NULL][envp...NULL][strings]` 布置、初始 SP 指向 `argc`。

但用户态仍是裸的：唯一用户程序是手写汇编 `init`（[user/init/init.s](user/init/init.s)），没有 C 运行时（crt0）、没有 syscall wrapper 库、没有 `/bin/sh`。本设计要在这些内核能力之上建立**最小可用用户态**，并把唯一缺失的内核暴露点 `SYS_EXECVE` 以 append-only 方式补上。

约束：freestanding；用户程序为 C、`-nostdlib -static`、`ET_EXEC`、bounded 体积（现上限 64KiB）；用户 libc 仅经 `int 0x80` 与内核交互；不改既有 syscall 号位/寄存器约定/向量/DPL/EOI 规则、boot/磁盘/exFAT 布局；单核、同步、无 SMP/锁。

## Goals / Non-Goals

**Goals:**
- 提供用户态 crt0：按现有初始栈布局正确取 `argc`/`argv`/`envp`、对齐、调用 `main`、经 `SYS_EXIT` 退出。
- 提供最小用户 libc：syscall wrapper 全集 + 负 errno -> `errno`/`-1` 翻译、最小字符串/内存、基于 `brk` 的有界 `malloc`/`free`、最小 stdio/`printf`。
- 提供 `/bin/sh`：读-解析-执行、内建命令、`fork`+`execve`+`wait` 外部命令、单级管道与基本重定向。
- 新增 `SYS_EXECVE`（append-only）暴露既有内核 exec 复用入口。
- 泛化用户程序构建/打包 target；默认 boot 进入 `/bin/sh`；新增默认关闭 `userland_smoke`。

**Non-Goals:**
- 完整 POSIX libc（locale/宽字符/完整 FILE 缓冲/分配器优化）、动态链接/共享库。
- 完整 shell 语义（作业控制、后台 `&`、变量/环境展开、glob、脚本控制流、多级复杂管道、子 shell）。
- `exec*` 全族（仅 `execve`）、`readline`/历史/行编辑、终端作业控制、C++ 用户程序、网络、SMP。

## Decisions

### 决策 1：crt0 直接复用现有初始栈布局，不引入新的内核栈约定
`copy_exec_args_to_stack` 已把 `argc` 放在初始 SP、其后是 `argv`（NULL 结尾）、再后是 `envp`（NULL 结尾）。crt0 `_start` 因此：从 `(rsp)` 读 `argc` -> `rdi`，`rsp+8` 作为 `argv` -> `rsi`，`rsp+8+(argc+1)*8` 作为 `envp` -> `rdx`，在 `call main` 前把栈按 16 字节对齐（System V），`main` 返回值在 `rax`，crt0 以其为 `SYS_EXIT` 参数。
- 替代方案：让内核改为传寄存器参数。否决：会改动既有 ELF 进入约定与 `user_elf_smoke` 路径，违背 append-only/非破坏原则。

### 决策 2：用户态自成一棵 `user/` 树，与内核 `cpp`/`ktl` 完全隔离
新增 `user/`：`user/crt0`（或复用 `user/init` 目录）、`user/libc`（头 + 实现 + 链接脚本）、`user/sh`、`user/bin/*` 测试程序。用户 libc 不 `#include` 任何内核私有头，仅共享一个事实来源：syscall 号与 errno 数值。为避免数值漂移，用户 libc 的 syscall 号/errno 以与 [syscall.h](include/bigos/syscall.h)、[errno.h](include/bigos/errno.h) 数值一致的方式声明（最简：用户侧自带一份镜像头并由源码契约测试断言其与内核头数值一致）。
- 替代方案：用户态直接 include 内核头。否决：内核头带 `namespace`/C++/freestanding 假设，污染用户 C 编译；且会把内核 ABI 细节泄漏进用户态。

### 决策 3：`SYS_EXECVE` 只做"暴露"，不新增内核 exec 语义
`SYS_EXECVE` dispatch 分支：经 VMA-backed 校验把用户 `path`（≤ `SYS_PATH_MAX_LEN`）与 `argv`/`envp`（受 `EXEC_MAX_ARGC`/`EXEC_MAX_ENVC`/`EXEC_MAX_STRING_BYTES` 约束）拷入内核 `ExecArgs`，经 VFS 读 ELF 到内核缓冲，调用既有 `exec_current_from_elf_image`。进入同步块 IO/分配前检查调度阻塞守卫。失败映射到确定性负 errno（`-ENOENT`/`-EACCES`/`-ENOEXEC`/`-E2BIG`/`-EFAULT`/`-ENOMEM`）。成功不返回。
- 替代方案：实现完整 `execve`（含内核侧 `PATH`、`#!` 解释器、`exec` 全族）。否决：超出最小可用用户态；`PATH` 查找改在用户态 shell 层做（见决策 7），内核 `SYS_EXECVE` 只认 shell 解析后的具体路径。

### 决策 4：常驻 C 版 init（`fork`+`execve`+`wait` 循环，收割孤儿）
默认 `/boot/user/init.elf` 改为一个链接了 crt0/libc 的最小 C init，承担真实 PID-1 语义：`fork` 子进程并在子进程 `execve("/bin/sh", ...)`，父 init 进入 `while(1) wait(...)` 常驻循环，回收所有退出的子进程（含被过继到 PID-1 的孤儿/僵尸）；当 `/bin/sh` 退出时 init 可重新 `fork`+`execve` 拉起 shell（重启策略在 tasks 固定，至少不得让 PID-1 退出）。init 自身的 `fork`/`execve`/`wait` 失败走确定性报错并经现有 `BIGOS_INIT_*`/panic 边界处理。`launch_init` 内核加载路径不变，只是被加载的 init 程序语义从"打印并退出"升级为"常驻派生 + 收割"。
- 替代方案 A：内核 `launch_init` 直接加载 `/bin/sh`。否决：把 shell 路径硬编码进内核、弱化 PID-1 语义，且 PID-1 即 sh 时无人收割孤儿。
- 替代方案 B：init 经 `SYS_EXECVE` 自替换为 `/bin/sh`（PID-1 变成 sh）。否决：实现虽更简单，但 PID-1 一旦退出系统就无 init、且孤儿无人回收，不符合真实 init 语义。
- 依赖（已核查）：常驻收割依赖"父进程先退出时孤儿被过继到 PID-1"的内核语义，而该语义**当前未实现**——[proc.cc](src/kernel/proc/proc.cc) `exit_current` 不 reparent 子进程，`mark_zombie_or_reap_pending` 在父不存在时直接自我回收（孤儿被静默回收、无人可 `wait`），且第 2087 行注释明示 "does not implement PID-1 restart/adoption"。因此**本阶段补齐最小过继接线**（见决策 9），而非仅记录差距。

### 决策 9：补齐最小孤儿过继到 PID-1（init）接线
在进程退出路径补一段最小 reparent：当一个进程退出时（`exit_current`/`fault_current_and_exit` -> 标记 zombie/reap 之前），遍历其 `first_child_pid` 兄弟链，把每个尚存活/僵尸的子进程 `parent_pid` 改为 `g_init_process->pid` 并挂入 init 的 `first_child_pid` 链；随后对每个已是 Zombie 的被过继子进程，向 init 投递 `SIGCHLD` 并 `wake_all(&g_process_wait_queue)`，使 init 的 `while(1) wait` 能收到并回收。复用现有 `Process.parent_pid`/`first_child_pid`/`next_sibling_pid` 字段与现有 sibling 链维护逻辑（参考 `publish_process`/`unpublish_process` 的挂链/摘链方式），不新增数据结构、不引入锁、不改 `wait_current` 既有遍历语义（它本就遍历父的子链，过继后孤儿自然落入 init 的子链）。
- 边界：init（PID-1）本身退出仍是异常情形，保持现有 `BIGOS_INIT_EXIT` -> idle/panic 边界不变；过继只处理"非 init 进程退出时其子进程的归属"。
- 失败安全：过继是纯链表指针改写，无分配、无 IO，不会失败；若 `g_init_process == nullptr`（init 已消失，异常路径）则跳过过继、维持现有自我回收兜底。
- 替代方案：不实现过继，仅记录差距与残留风险。否决：常驻 init 收割孤儿是其核心价值，缺过继则被 init 间接拉起的程序（经 `fork` 的孙子进程在中间层退出后）会变成无人收割的孤儿，违背决策 4 的 PID-1 语义。

### 决策 10：阶段 19 允许收敛真实用户态链路暴露出的最小内核修复
本阶段的主要目标仍是用户态运行时，但真实 C 程序、`fork`+`execve`、阻塞 `wait`、管道和按需分页组合后，暴露出若干此前 smoke 未覆盖的内核边界问题。为保证默认 boot 能进入 `/bin/sh`、`userland_smoke` 能确定性通过，以下修复纳入本 change，且都保持 append-only/局部语义，不改变既有 syscall 号位、向量、磁盘布局或内核地址布局：
- **启用 NXE**：用户数据/栈 PTE 带 `NO_EXECUTE`，若 EFER.NXE 未打开，bit63 会成为 reserved bit 并在真实用户栈/数据访问时触发 `#PF`。在长模式启用路径同时设置 EFER.NXE，匹配现有页属性语义。
- **#PF 恢复门控**：`page_fault_handler` 返回 `bool`，当 `try_handle_user_page_fault` 成功物化 demand-zero/COW 页时直接恢复故障指令，不再落入 `default_exception_handler`。
- **#PF 分配前置**：真实 ring3 `#PF` 在 `NonblockingContextGuard` 且 IF=0 下运行，`can_block()` 过强，会拒绝合法的缺页物化。引入 `sched::can_allocate_in_fault()`，仅要求普通 Running 线程且非调度临界区；物理帧分配路径不阻塞，满足 #PF 上下文需求。
- **fork 入口帧的真实用户 `rsp/ss`**：`InterruptFrame.rsp/ss` 是 `isr_common` 同步槽，不是 CPU iret tail 中的真实 ring3 `rsp/ss`。`fork_current` 必须从 `InterruptFrame` 后方的 CPU iret frame 捕获真实值，子进程才能 `iretq` 回用户栈而非 kernel rsp/null ss。
- **ELF 映射不污染 active root**：`create_elf_user_process` 只把 ELF segment/stack 页映射到目标进程 root；不再同步映射到当前 active root，避免默认 init 或 `execve` 在 kernel root / 其他进程 root 上残留低半区映射，导致后续 exec 同地址 `MapFailed`。
- **调度恢复用户线程上下文**：`fork` 后存在多个用户 kernel-thread，阻塞 syscall 可切换到另一个用户线程；后者退出/exec 会改写全局 `g_current_process`、CR3 与 TSS `rsp0`。scheduler 为 TCB 记录关联的 `Process*`，每次线程恢复时重建当前进程、用户 CR3 与 TSS `rsp0`，保证阻塞 syscall 中途恢复和返回 ring3 都使用正确地址空间。
- **fd 1 控制台快路径条件化**：stdout 串口快路径仅在 fd 1 未绑定真实文件时生效；当 `pipe`/`dup2`/重定向把文件安装到 fd 1 时，`write(1, ...)` 必须走 fd 表，否则单级管道会被错误写到 COM1。

### 决策 5：最小 `malloc` 用 `brk` bump + 简单自由链表
基于 `SYS_BRK` 线性扩展堆；`malloc` bump 分配，`free` 入简单自由链表并对相邻/末块做最小合并（或先纯 bump + freelist 复用，合并可选）。容量受 `brk` 失败约束，失败返回 NULL，不破坏既有块。
- 替代方案：实现 dlmalloc 级分配器。否决：超出最小目标，且 freestanding 单核下无并发需求。

### 决策 6：shell 容量全部 bounded 且编译期固定
`SH_MAX_LINE`（如 256）、`SH_MAX_ARGC`（如 32）、`SH_MAX_PIPE_SEGMENTS`（本阶段 2，即单级管道）、重定向每命令最多各一个 `>`/`<`。超限确定性报错并回到读行循环。

### 决策 7：shell 支持用户态 `PATH` 查找
shell 在用户态层实现命令查找：命令名含 `/`（绝对或相对路径）时直接交给 `execve`；不含 `/` 时按 `PATH` 环境变量（缺省回退到固定默认，如 `/bin`）顺序，对每个目录拼接命令名逐一尝试 `execve`，命中即运行，全部失败则报 "command not found"。`PATH` 解析依赖最小 `environ`/`getenv`（仅读取，不实现完整环境数据库管理），其值经 init -> shell 的 `envp` 传入。候选目录数与拼接路径长度 MUST 有界（复用 `SYS_PATH_MAX_LEN` 约束）。
- 替代方案：仅接受绝对/相对路径，不查 `PATH`。否决：交互体验差，且 `PATH` 是纯用户态增量、内核 `SYS_EXECVE` 不受影响。
- 注：`PATH` 查找完全在 shell 内完成；内核 `SYS_EXECVE` 仍只接收 shell 解析后的具体路径，决策 3 不变。

### 决策 8：用户 libc 的 syscall/errno 头采用"镜像头 + 契约测试"
`user/libc` 自带一份纯 C 的 syscall 号镜像头与 errno 镜像头，数值与内核 [syscall.h](include/bigos/syscall.h)/[errno.h](include/bigos/errno.h) 一致；新增源码契约测试（`tests/`，`uv run pytest`）断言用户镜像头与内核头逐项数值相等，内核改号而用户头漏改时测试立即失败。
- 替代方案：由内核头自动生成用户头。否决：需维护一个解析内核 C++ 头（`enum`/`#define`）的生成器，给构建加隐藏魔法；项目偏好显式、易审查，镜像头 + 契约测试用一份小重复换取透明度，漂移风险由测试兜底。

### 控制流（normal boot -> init -> shell）
```
kernel: proc::init() -> launch_init()
  -> vfs::init -> read /boot/user/init.elf -> create_elf_user_process -> run_user_process (ring3)
user(init.main, PID-1): loop {
    pid = fork()
    child: execve("/bin/sh", argv, envp)   [SYS_EXECVE]
    parent(init): wait(...)  // 收割 sh 与被过继的孤儿；sh 退出后重新 fork 拉起
  }
  child kernel: copy args -> vfs read /bin/sh -> exec_current_from_elf_image -> 进入 sh 入口 (crt0)
user(sh): crt0 _start -> main -> loop{ read line -> parse -> builtin? :
    PATH 查找具体路径 -> fork+execve+wait / pipe / redirect }
```

### 数据流（execve 用户缓冲校验）
```
user rdi=path, rsi=argv, rdx=envp (int 0x80, rax=SYS_EXECVE)
 -> dispatch: 校验阻塞守卫
 -> VMA 校验并拷入 path/argv/envp 到内核 ExecArgs (bounded)
 -> vfs::open_absolute + bounded read -> 内核 image buffer
 -> exec_current_from_elf_image(image, len, args)  // 成功不返回；失败 -> 释放并回写 -errno
```

## Risks / Trade-offs

- [crt0 栈对齐/`argv` 偏移与内核布局不一致导致 main 取参错误] → 以 `copy_exec_args_to_stack` 实际布局为唯一事实来源；先用 `userland_smoke` 断言 argc/argv 内容与退出码；必要时 Bochs/QEMU 交叉验证 ring3 进入。
- [用户 libc syscall 号/errno 与内核头数值漂移] → 源码契约测试断言用户侧镜像头与 [syscall.h](include/bigos/syscall.h)/[errno.h](include/bigos/errno.h) 数值一致。
- [`SYS_EXECVE` 用户指针/`argv` 数组校验不严导致内核读越界] → 复用既有 VMA-backed 校验与 `EXEC_MAX_*`/`SYS_PATH_MAX_LEN` 上界；非法一律 `-EFAULT`/`-E2BIG`，不进入 exec。
- [execve 失败后当前进程镜像处于半替换状态] → 在调用 `exec_current_from_elf_image` 前完成所有可失败的拷入/读盘；该入口本身的"先校验后替换"语义不变，失败保持原镜像可继续。
- [用户程序体积超 64KiB 上限（C + libc 链接后膨胀）] → 保持最小 libc、`-Os` 选项；上限是 bounded 约束不可去除，按实测链接体积在 tasks 中收口（不超则保持，超则调高到新有界值并在注释说明原因/新值）；构建超限始终确定性失败。
- [常驻 init 依赖的"孤儿过继到 PID-1"语义当前未实现] → 核查确认缺失；本阶段在退出路径补齐最小 reparent 接线（决策 9），把退出进程的子进程过继给 init 并对僵尸子进程唤醒 init 的 `wait`；过继为纯指针改写、无分配/IO、不会失败；`g_init_process == nullptr` 时跳过并回退到现有自我回收兜底。
- [真实用户态链路组合出此前 smoke 未覆盖的内核边界] → 纳入决策 10 的最小修复集合：NXE、#PF 成功恢复门控、#PF 分配前置、fork 真实 `rsp/ss`、ELF 映射不污染 active root、调度恢复用户线程上下文、fd 1 控制台快路径条件化。每项均有源码契约测试或 QEMU serial-marker smoke 覆盖。
- [shell `PATH` 查找逐目录 `execve` 尝试导致多次失败 syscall / 路径越界] → 候选目录数与拼接路径长度有界（复用 `SYS_PATH_MAX_LEN`）；仅对 `execve` 返回 `-ENOENT` 继续下一候选，其他错误立即停止并报错。
- [shell 管道/重定向 fd 泄漏或 close-on-exec 处理错误] → 复用阶段 18 的 `dup2`/close-on-exec 语义；shell 在 `fork` 子进程内显式 close 不需要的管道端。
- [默认 boot 进入交互 shell 后自动化 smoke 卡在等待 stdin] → `userland_smoke` 走独立验证程序/路径并自带确定性输入或非交互断言，不依赖人工输入；交互 shell 仅在图形 QEMU 手工冒烟。

## Migration Plan

1. 先落地内核侧 `SYS_EXECVE`（append-only 号 + dispatch 分支，复用既有 exec 入口与校验），不改其他 syscall。
2. 落地 `user/crt0` + `user/libc`，先以一个最小 C 测试程序（替换/扩展 init）验证 crt0 取参与退出码。
3. 落地 `/bin/sh` 与若干 `/bin/*` 测试二进制；泛化 xmake 用户程序 target 与镜像打包。
4. 在内核退出路径补齐最小孤儿过继接线（决策 9），再把默认 `/boot/user/init.elf` 切到常驻 C 版 init（`fork`+`execve("/bin/sh")`+`while(1) wait`）；保留旧汇编 init 路径作为可回退参考。
5. 新增默认关闭 `userland_smoke` 与 marker；接入 QEMU headless serial-marker 验证；补源码契约/行为断言测试。
- 回滚：`SYS_EXECVE` 为 append-only，移除其分支与用户态产物即可回到阶段 18 行为；init 可切回旧汇编 init。

## Open Questions

以下问题已收敛为决策（见对应 Decisions 小节）：

- init 形态：采用**常驻 C 版 init（`fork`+`execve`+`wait` 循环，收割孤儿）**，不采用自替换。见决策 4。
- 孤儿过继到 PID-1：核查确认现有内核**未实现**，本阶段**补齐最小 reparent 接线**（退出时把子进程过继给 init 并唤醒其 `wait`）。见决策 9。
- 命令查找：shell **支持用户态 `PATH` 查找**（命令名含 `/` 直走路径，否则按 `PATH`/默认 `/bin` 逐目录尝试）。见决策 7。
- libc 头来源：采用**镜像头 + 契约测试**，不采用由内核头生成。见决策 8。
- 用户程序体积上限：**用实测链接体积收口**——保持 64KiB 直至实测超限，超限则调高到新有界值并在注释说明；上限不可去除、构建超限始终确定性失败。见 Risks 对应条目与 tasks 5.2。

无剩余阻塞性 open question。
