## Context

当前 `kernel/core/proc` 已能创建 embedded first-user-program smoke 和 filesystem-backed ELF smoke，并能通过 `int 0x80` 执行 `SYS_WRITE`/`SYS_EXIT`、处理 CPL3 fault、切回 kernel CR3 并延后回收资源。但该路径仍由 `BIGOS_USER_PROCESS` smoke 开关控制，依赖固定 PID、单个 `g_current_process`、单个 `g_reap_pending_process`，不具备常规进程表、父子关系、`wait` 或 general `exec argv/envp` 语义。

阶段 12 的设计目标是在不引入 `fork`、COW、fd/VFS、demand paging 或 POSIX 大面策略的前提下，把这条 smoke runtime 提升为普通内核子系统。实现必须继续保持 x86_64 单核、Legacy BIOS/MBR/exFAT、read-only filesystem、同步加载、safe teardown 和 freestanding 约束。

## Goals / Non-Goals

**Goals:**

- 让 process core 在非 user smoke 配置下可构建，并提供稳定的 `proc::init()`、PID 分配、进程表和生命周期状态机。
- 支持最小父子关系：父进程可创建/exec 子进程、观察 zombie、执行 blocking `wait`，并回收 exit status。
- 将 `SYS_EXIT`、用户 fault termination 和 loader failure 统一到 `Terminated`/`Faulted` -> `Zombie`/`ReapPending` -> `Reaped` 的安全路径。
- 将 bounded ELF64 `ET_EXEC` loader 拆成可复用的 `exec` primitive，支持基础 `argv`/`envp` 用户栈布置，并继续复用 read-only exFAT 读取。
- 保持当前 first-user-program smoke 和 user-ELF smoke 可复现，作为 process lifecycle 的验证消费者。

**Non-Goals:**

- 不实现 `fork`、COW、signals、process groups、sessions、permissions、完整 POSIX wait variants 或 kill 语义。
- 不实现 fd table、VFS、`open`/`read`/`close` 用户 API、文件描述符继承、page cache、writable filesystem 或 async I/O。
- 不实现 VMA、`mmap`、`brk`、demand paging、user stack growth、dynamic linking 或 user-space libc。
- 不引入 SMP、IPI、per-CPU process table、cross-CPU locking、TLB shootdown、UEFI/OVMF backend 或新的 storage driver。

## Decisions

- 常规 process core 与 smoke entry 分离。将 PID、进程表、生命周期状态、父子链表、wait queue 和 reaper queue 编译为常规内核能力；embedded/user-ELF smoke 只负责触发特定启动场景。替代方案是继续让 `kernel/core/proc` 只随 smoke 编译，但这会阻塞 fd/VFS 和 VMA 后续阶段复用进程所有权。
- 使用固定容量或显式有界的单核进程表。第一版通过稳定 PID 和 bounded table/list 避免动态增长策略、锁复杂度和 SMP 语义；容量耗尽时返回确定性错误或 panic/marker。替代方案是使用无限动态表，但会把 allocator failure、reclaim 和 fork 前置得过早。
- 将 `exit` 与 resource teardown 分离。`SYS_EXIT` 和用户 fault 只记录 status、切换到安全 kernel root、使当前执行流不可返回，并唤醒等待者；真正释放 user root、用户页、dynamic page-table pages、kernel stack 和 process object 必须在 safe reaper 上下文执行。替代方案是在 syscall/fault 当前栈上立即释放，但会破坏现有 active stack/CR3 安全边界。
- `wait` 复用现有 blocking primitives，并收敛为单一 syscall 形态。第一版只提供 `wait(pid, status*)`；`pid == BIGOS_WAIT_ANY` 表示等待任一子进程，其他非零 PID 表示等待指定子进程，不额外引入独立 `wait_any()` syscall。父进程等待子进程时进入可唤醒 wait queue；子进程进入 zombie 时 wake parent。若无可等待子进程则返回确定性错误；若有匹配子进程但尚未退出则阻塞。替代方案是 busy polling 或暴露多个 wait variants，但前者破坏阶段 10 blocking 模型，后者会过早扩大 POSIX wait 面。
- `exec` 采用 in-place image replacement 的最小语义。本阶段只支持在一个 process 内加载 bounded ELF64 `ET_EXEC`，建立新用户 root/stack/argv/envp 后再切换；commit 前失败必须 rollback 新 image 并保留旧 image，commit 后失败统一终止当前进程并记录 deterministic exec failure status，不尝试恢复旧 image。替代方案是先实现 `fork+exec` 或 commit 后恢复旧 image，但前者依赖尚未稳定的 `fork`/COW/VMA，后者会显著增加 active CR3/root 和资源所有权复杂度。
- 初始用户栈采用最小 libc-like 形状。第一版布置 `argc`、`argv[]`、`envp[]` 和字符串内容，省略 auxv、动态链接器约定、TLS 和 libc startup 扩展；用户入口约定必须在 BigOS 文档中固定。内核从 bounded kernel-side vector 或 smoke-provided 参数复制字符串，拒绝越界、过多参数、过长字符串或栈空间不足。替代方案是 BigOS 私有寄存器约定，但会降低后续 userland/libc 迁移的连续性。
- 保持 ABI 和地址布局不变。`int 0x80` vector、GDT/TSS/RSP0、`iretq` ring3 entry、kernel higher-half、direct map、KVMEM、recursive self-map、ELF entry/user low-half bound 和 raw image/exFAT 路径不因本阶段移动。

## Risks / Trade-offs

- 进程表状态机过早泛化 -> 通过固定状态集合、单核文档和 smoke consumers 控制范围，禁止引入 POSIX 大面策略。
- `wait` 与 scheduler/blocking 交互出错 -> 只允许在可阻塞的进程上下文等待，禁止 IRQ/preemption-disabled/critical-section 内阻塞，并增加 source-level 检查。
- `exec` 失败时资源泄漏或破坏旧 image -> 采用 prepare-then-commit 流程，所有新 root/pages/stack/loader buffer 在 commit 前独立 owner；失败 rollback 不触碰仍在运行的旧 image。
- zombie/reaper 回收 active stack 或 active CR3 -> 保留现有 active-root/current-stack 检查，把 process object 释放推迟到安全 kernel context。
- smoke 行为与常规进程核心耦合过强 -> 保持 smoke 开关只控制启动入口和验证 marker，不控制 process core 的基础编译。
- QEMU/Bochs runtime marker 不稳定 -> validation 记录工具可用性、串口日志、跳过原因和残余 bootability 风险，不能把未运行的 emulator smoke 声称为通过。

## Migration Plan

- 第一步：调整 build gating，让 process core 默认可编译；将 embedded first-user-program 和 user-ELF smoke 入口保留在独立开关下。
- 第二步：引入 PID allocator、process table、状态机、父子 linkage、zombie/reap queue，并把当前单实例全局指针替换为 current process + table lookup。
- 第三步：迁移 `SYS_EXIT`、CPL3 fault termination 和 idle/reaper 回收路径，确保 wait queue wakeup 和 safe teardown 顺序可验证。
- 第四步：拆分 ELF loader 为 passive validate/map 和 committed exec 两段，增加 bounded `argv`/`envp` 初始栈布置。
- 第五步：更新 first-user-program/user-ELF smokes、source-level checks、QEMU headless markers 和 OpenSpec validation。
- 回滚策略：若 general process path 不稳定，保留 smoke entry 的构建开关和旧 marker expectation，可临时禁用 normal process entry，但不能重新把 process core 隐藏到 smoke-only 编译路径。

## Resolved Decisions

- 第一版 `wait` syscall 使用单一 `wait(pid, status*)` 入口，并用 `BIGOS_WAIT_ANY` 表达 wait-any 语义，不新增独立 `wait_any()` syscall。
- `exec` commit 前失败 rollback 新 image 并保留旧 image；commit 后失败统一终止当前进程并记录 deterministic exec failure status。
- 初始用户栈采用最小 libc-like `argc`/`argv`/`envp` 形状，不提供 auxv、dynamic linker、TLS 或完整 libc startup ABI。
