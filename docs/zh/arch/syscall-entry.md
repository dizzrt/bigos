# 系统调用入口（syscall entry）

BigOS user entry and syscall capability 使用一条受控的“软件主动进入内核”路径与最小 syscall ABI。ring0 syscall diagnostic capability 的 ring0 诊断 syscall 仍保留；默认关闭的 `user_program_smoke` 路径会在配置 GDT/TSS 与用户地址空间后允许 CPL3 通过 `int 0x80` 进入同一个 dispatcher。

## 入口机制选择：`int 0x80` 软件中断门

本阶段采用 `int 0x80` 软件 gate，而非 `syscall`/`sysret` 快速系统调用：

- 复用既有 kernel-owned 静态 IDT + `interrupt.s` 的 `isr_common` + `irq_dispatch` 框架。vector `0x80` 的 `isr_entry` stub 与 dispatch 框架已存在，因此几乎是“零新增汇编”的入口：只需在 `irq_dispatch` 中识别 syscall vector 并路由到 syscall dispatcher。
- 取舍：`syscall`/`sysret` 需要配置 `IA32_STAR/LSTAR/FMASK` MSR、定义内核/用户段排列约束、并准备 `swapgs`/内核栈策略；当前 change 继续使用更可解释的 IDT gate + TSS/RSP0。
- DPL：仅 `VECTOR_SYSCALL` gate 配置为 DPL=3。它是 trap gate，使普通进程 syscall 保留 IF，fd/VFS syscall 可以通过 `sched::can_block()`。其它 CPU exception 与 i8259 IRQ gate 保持 ring0-only interrupt gate。syscall 仍不是外部 IRQ，dispatch 路径不发送 i8259 EOI。
- vector 用具名常量 `VECTOR_SYSCALL = 0x80` 固定，集中声明在 `include/irq/interrupt.h`，避免散落魔数。

## 最小 syscall ABI

syscall number、参数、返回值与 `InterruptFrame` 字段的对应关系（声明在 `include/bigos/syscall.h`，并由源码级检查断言）：

| 角色          | 寄存器 | `InterruptFrame` 字段 |
| ------------- | ------ | --------------------- |
| syscall number | `rax`  | `InterruptFrame.rax`  |
| 参数 0        | `rdi`  | `InterruptFrame.rdi`  |
| 参数 1        | `rsi`  | `InterruptFrame.rsi`  |
| 参数 2        | `rdx`  | `InterruptFrame.rdx`  |
| 参数 3        | `r10`  | `InterruptFrame.r10`  |
| 参数 4        | `r8`   | `InterruptFrame.r8`   |
| 参数 5        | `r9`   | `InterruptFrame.r9`   |
| 返回值        | `rax`  | dispatcher 写回 `InterruptFrame.rax` |

- syscall number 通过 `rax` 传入；返回值通过 `rax` 写回，即 dispatcher 写 `InterruptFrame.rax`，调用方在 `iretq` 返回后从 `rax` 读取结果。
- 第 4 个参数使用 `r10`（而非 `rcx`），贴近 SysV/Linux x86_64 syscall 约定，并避免与 `int 0x80` / `iretq` 下被破坏的 `rcx` 语义冲突。
- 除返回值外的寄存器约定为 callee 可 clobber，调用方负责保存。
- ABI 与具体入口机制解耦：dispatcher 以 `InterruptFrame` 为输入；未来若换 `syscall`/`sysret` 只需替换入口 stub，ABI 与 dispatch 可复用。

## dispatch 与未知 number 处理

`bigos::sys::dispatch(InterruptFrame*)`：

- 从 `InterruptFrame.rax` 读取 number，用 bounded switch 路由到内核实现。
- 已知 number 调用对应实现，返回值经 `rax` 写回。
- 未知 number 在 `rax` 写入确定性负错误码 `-bigos::ENOSYS`（数值 `-38`），不崩溃、不进入 CPU 异常路径。POSIX 风格错误码统一以正值集中定义于 `include/bigos/errno.h`，写入返回寄存器时取负。
- 在 `irq_dispatch` 中通过 `is_syscall_vector(vector == VECTOR_SYSCALL)` 识别 syscall，命中后调用 `bigos::sys::dispatch` 并直接返回。该路径 **MUST NOT** 发送 i8259 EOI（syscall 不是外部 IRQ）；CPU 异常、外部 IRQ、syscall 三类入口的 EOI 语义保持分离不变。

## 诊断型 syscall

- `SYS_DEBUG_WRITE`（number=0）：把内核内固定/受限 buffer 经现有 serial/console 输出确定性 marker `BIGOS_SYSCALL_WRITE`，并返回写出的字节数。本阶段调用方为内核态，buffer 为内核内 bounded 来源；**不做用户指针校验**。
  - **ring3 前置项**：引入 ring3 后，用户态传入的 buffer 指针与长度 **必须** 做用户地址空间范围校验与 bounded 拷贝后才能输出。
- `SYS_GET_TICK`（number=1）：返回 `bigos::timer::ticks()` 单调 tick，验证返回值寄存器路径。`timer::ticks()` 已通过 `include/bigos/timer.h` 稳定暴露，是 context-agnostic bounded read，故选用它而非 `SYS_DEBUG_NOOP`。
- `SYS_WRITE`（number=2）：仅支持早期 console sink（当前固定 `fd=1`），在读取用户 buffer 前检查低半区范围、页表 present/user bit 和最大长度 `SYS_WRITE_MAX_LEN`，再把 bounded 内容输出到 serial/VGA，并返回确定性字节数或 `-bigos::EFAULT`。
- `SYS_EXIT`（number=3）：记录当前用户进程 exit code，标记 terminated，恢复内核地址空间并转入 scheduler 的延后回收退出路径；该 syscall 不返回到已终止用户指令流。
- `SYS_WAIT`（number=4）：在调用方可阻塞时等待子进程状态，可选地把有界 raw exit status 拷贝到用户 `int*`；不支持或不可阻塞上下文返回确定性 wait 错误。
- `SYS_OPEN`（number=5）：复制有界 NUL 结尾用户 path，只接受 read-only flags，经 VFS 壳层 open，并返回 process-local fd。
- `SYS_READ`（number=6）：验证用户目标 range，经进程 fd table 与 VFS file offset 读取到有界 kernel buffer，再 copy out，并返回 byte count。
- `SYS_CLOSE`（number=7）：关闭 process-local fd 并 drop open-file reference。

随后是 `SYS_BRK`（8）、`SYS_MAP_ANON`（9）、`SYS_FORK`（10）。只读身份/时间查询号位追加在其后，全部不阻塞、不分配、不发 EOI：

- `SYS_GET_TIME`（number=11）：返回当前墙钟时间的 Unix epoch 秒（`bigos::time::current_unix_time()`），参见 `docs/zh/arch/wall-clock-and-identity.md`。
- `SYS_GETPID`（number=12）/ `SYS_GETPPID`（number=13）：返回当前进程的 `pid` / `parent_pid`。
- `SYS_GETUID`（number=14）/ `SYS_GETGID`（number=15）：返回当前进程的 `uid` / `gid`。

其后是 `SYS_KILL`（16）、`SYS_SIGACTION`（17）、`SYS_SIGPROCMASK`（18）、`SYS_SIGRETURN`（19）、`SYS_LSEEK`（20）、`SYS_PIPE`（21）、`SYS_DUP`（22）、`SYS_DUP2`（23）、`SYS_FSYNC`（24）、`SYS_MKDIR`（25）、`SYS_UNLINK`（26）。

- `SYS_EXECVE`（number=27）：以 append-only 方式把内核内已有的当前进程镜像替换路径（`exec_current_from_elf_image` + 只读 VFS 读路径）暴露给 CPL3。ABI：`rdi` = 用户 `path`，`rsi` = `argv`（NULL 结尾用户指针数组），`rdx` = `envp`。path 受 `SYS_PATH_MAX_LEN` 约束，argv/envp 向量受 `EXEC_MAX_ARGC` / `EXEC_MAX_ENVC` / `EXEC_MAX_STRING_BYTES` 约束；所有用户缓冲先经 VMA-backed 校验拷入再使用。成功时以新 ELF64 `ET_EXEC` 镜像替换当前进程地址空间并进入新程序入口，因此不返回调用点。失败时返回确定性负 errno（`-ENOENT`、`-EACCES`、`-ENOEXEC`、`-E2BIG`、`-EFAULT`、`-ENOMEM`、`-EWOULDBLOCK`、`-EIO`），调用镜像可继续执行。与其他 fd/VFS syscall 一样，它在分配或进入同步存储 IO 前检查 `sched::can_block()` 守卫；不改动任何既有 syscall 号、寄存器约定、`VECTOR_SYSCALL` / DPL 布局或「syscall 不发 EOI」规则。
- `SYS_READDIR`（number=28）：从已打开 fd 读取有界目录项批次到用户 `struct bigos_dirent[]`。ABI：`rdi` = fd，`rsi` = 用户 entries 缓冲区，`rdx` = 请求项数。请求受 `SYS_DIRENT_MAX_ENTRIES` 约束，经用户缓冲校验 copy out，返回 entry count 或确定性负 fd/VFS errno。
- `SYS_STAT`（number=29）/ `SYS_FSTAT`（number=30）：把有界元数据复制到用户 `struct stat`。`SYS_STAT` 使用 `rdi` = 用户 path、`rsi` = 用户输出指针；`SYS_FSTAT` 使用 `rdi` = fd、`rsi` = 用户输出指针。它们只暴露当前文件/目录元数据子集，不表示完整 POSIX `stat(2)` 语义。
- `SYS_CHDIR`（number=31）：使用 `rdi` = 用户 path；只有在完成有界路径复制、解析并确认目标为目录后，才提交进程 cwd。
- `SYS_GETCWD`（number=32）：使用 `rdi` = 用户缓冲区、`rsi` = 缓冲区大小；复制 NUL 结尾 cwd，或返回确定性 `-ERANGE`、`-EFAULT`、`-EINVAL`。
- `SYS_RENAME`（number=33）：使用 `rdi` = 旧用户 path、`rsi` = 新用户 path。它受限于当前有界可写 `/rw` regular-file rename 语义，不表示完整持久文件系统或跨设备 rename 支持。

syscall dispatcher 保持 exception/IRQ/syscall 的 EOI 分离不变。CPU exception 与外部 IRQ 仍是 nonblocking context。fd/VFS syscall 在分配或进入同步 ATA PIO/exFAT read 前检查 `sched::can_block()`；普通用户进程 syscall 可通过该 guard，因为 DPL=3 trap gate 会保留 IF。

用户态 raw syscall primitive `syscall0` 到 `syscall6` 仍是 BigOS-specific 低层 helper。它们把 number 与返回值绑定到 `rax`，参数绑定到 `rdi`、`rsi`、`rdx`、`r10`、`r8`、`r9`，并列出 `rcx`、`r11` 与 `memory` clobber。源码级 contract 测试会检查这些约束，避免 wrapper 修改静默偏离 ABI；更高层 libc wrapper 仍负责把内核负返回值翻译为正 `errno` 与接口文档化的失败哨兵。

## 验证：默认关闭构建开关 + 确定性 marker

默认关闭的 xmake 开关 `syscall_smoke`（`xmake f --syscall_smoke=y`）继续从 ring0 验证 `SYS_DEBUG_WRITE`、`SYS_GET_TICK` 和未知 number。额外默认关闭的 smoke 覆盖 flat 首个用户程序、filesystem-backed user ELF、demand paging、fork/COW、time/identity、signals、writable FS、pipes 和 userland runtime。普通启动现在会打包 `/boot/user/init.elf`，进入常驻 PID-1 init，并启动 `/bin/sh`；默认 headless 验证观察 `BIGOS_USER_EXEC`。

## 本阶段非目标

- 不切换到 `syscall`/`sysret` MSR 快速路径。
- 不把当前 bounded syscall 集解释为完整 POSIX-wide syscall 语义、用户线程、作业控制、动态链接或完整 libc。
- 不把 demand paging/COW 扩展到当前 bounded anonymous mapping 之外，也不引入广泛 file-backed `mmap`。
- 不对 syscall 以外的 IDT gate 放宽 DPL；不从 syscall path 发送 i8259 EOI。

## 横切工程化项

本 change 未修改 `tools/boot_debug.py`。若后续需要它自动注入 `syscall_smoke` 开关并观测 `BIGOS_SYSCALL_*` marker，应作为单独的横切工程化项处理，不把 Python 修改混入本 change，除非明确扩展任务范围。
