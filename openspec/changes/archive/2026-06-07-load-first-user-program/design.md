## Context

kernel thread scheduler capability 已将 `kernel()` 尾部裸 `hlt` 收敛为 scheduler-owned idle 线程；ring0 syscall diagnostic capability 已归档两块用户态前置能力：

- `user-address-space-vmem`：显式 `PageAttr` / `page_attr` 策略、`map_page` / `unmap_page` primitive、复制内核高半区且低半区独立的用户地址空间根派生。
- `syscall-entry`：复用 kernel-owned 静态 IDT 与 `InterruptFrame` dispatch ABI，以 `int 0x80` 建立 `VECTOR_SYSCALL = 0x80`，固定 `rax`/`rdi`/`rsi`/`rdx`/`r10`/`r8`/`r9` ABI，并实现诊断 syscall。

ring0 syscall diagnostic capability 有意不切 CR3、不进 ring3、不加载用户程序，并保持 `int 0x80` gate DPL=0。user entry and syscall capability 的目标是把这些准备工作组合成首个用户态闭环：内核创建一个最小进程，加载内置用户程序，构造用户栈与 ring3 进入帧，用户程序通过 syscall 输出 marker 并调用 exit 返回内核。

约束：x86_64 单核、Legacy BIOS/i8259/PIT、freestanding C++17、无 hosted libc、无异常/RTTI、无 SMP；不改动 boot 固定地址、higher-half base、kernel load base、BootInfo ABI、direct map、`KVMEM_BASE` 或 self-mapping 地址布局；保持 exception/IRQ/syscall 三类入口的 EOI 语义分离。

## Goals / Non-Goals

**Goals:**

- 定义最小进程模型：用户地址空间根、用户入口、用户栈、关联内核线程/内核栈、退出状态和 bounded 生命周期。
- 加载第一个用户程序：优先使用内核内嵌或构建期打包的最小 ELF / 等价镜像，不依赖内核文件系统。
- 建立 ring3 进入路径：准备用户段、TSS/RSP0 或等价内核栈返回机制，构造 `iretq` frame 进入用户入口。
- 让 `int 0x80` 可从 ring3 触发，并提供最小 `write` / `exit` 闭环。
- 为用户态 `#PF` 或非法用户内存访问提供受控诊断/终止路径，不实现恢复。
- 提供默认关闭 smoke 与源码级验证，证明用户程序启动、syscall write、exit 和错误边界可观测。

**Non-Goals:**

- 不实现 `fork`、`exec`、`wait`、signal、shell、文件描述符表、VFS 或 POSIX 完整 syscall 表。
- 不实现 demand paging、copy-on-write、用户堆、`mmap`、动态链接或用户态 loader。
- 不实现抢占调度、多进程公平性、SMP、per-CPU run queue 或跨 CPU TLB shootdown。
- 不从块设备或文件系统加载用户程序；首个程序来自构建产物或内核内嵌镜像。
- 不切换到 `syscall`/`sysret` MSR 快速路径；继续使用ring0 syscall diagnostic capability 的 `int 0x80` ABI。

## Decisions

### Decision: 首个用户程序使用内嵌镜像而非文件系统

实现阶段新增一个最小用户程序构建产物（例如独立链接的 freestanding ELF64 或二进制 blob），由 `xmake.lua` 以符号区间或链接对象形式编入内核。内核进程加载器解析该镜像的少量必要元数据，映射用户代码、只读数据、数据/BSS 和用户栈。

理由：kernel block and filesystem read capability 才计划块设备与文件系统服务。user entry and syscall capability 的核心风险是 ring3、页表、syscall 和退出路径，内嵌镜像可以避免把 FS/驱动不确定性混入首个用户态闭环。

替代方案：复用 bootloader 的 exFAT 读取能力在内核中加载 `/bin/init`。否决理由：当前 exFAT 能力属于引导期一次性读取，不是内核内块设备/FS 服务；提前复用会扩大user entry and syscall capability 范围。

### Decision: 进程模型保持单进程 bounded 形态

新增 `bigos::proc` 或等价子系统，进程对象记录 PID、地址空间根、用户入口、用户栈范围、状态和 exit code。user entry and syscall capability 只要求创建并运行一个 smoke 用户进程；可预留 bounded 表或单例 slot，不声明通用多进程管理能力。

理由：最小进程对象可以把地址空间、用户入口和退出状态从内核线程中分离出来，同时避免过早设计 PID namespace、wait queue、进程树或资源回收。

替代方案：直接从某个内核线程 `iretq` 到用户入口，不引入进程对象。否决理由：会把地址空间、退出状态和 syscall 当前进程语义散落在调度器/内核入口代码里，不利于后续扩展。

### Decision: 用户地址空间激活只发生在受控进程运行路径

ring0 syscall diagnostic capability 的 `derive_user_address_space_root()` 仍只是准备 API，不因派生而切换 CR3。user entry and syscall capability 在 `run_user_process()` 或等价路径中显式激活派生根，并要求该根共享内核高半区、direct map、KVMEM 和 self-mapping 所需条目。切换前后必须记录当前/目标根，失败时走 panic/marker 或受控终止。

理由：把“派生”和“激活”拆开可以保留ring0 syscall diagnostic capability 的安全边界，同时让user entry and syscall capability 明确承担 CR3 切换风险。

替代方案：让派生 API 直接切换到用户地址空间。否决理由：会让页表构造与执行上下文切换耦合，难以在源码级测试中隔离验证。

### Decision: ring3 进入使用 `iretq` frame，syscall 继续使用 `int 0x80`

进入用户态时构造完整 `iretq` 返回帧（user RIP、CS、RFLAGS、RSP、SS），并在必要时配置 GDT 用户 code/data selector 与 TSS/RSP0，使 CPL3 syscall/exception 能返回内核栈。ring0 syscall diagnostic capability 的 `int 0x80` dispatcher 保持 ABI 不变，但 `VECTOR_SYSCALL` gate 需要允许 CPL3 触发。

理由：当前 ISR common path 已以 `iretq` 返回，`int 0x80` stub 已覆盖所有 256 vector。`iretq` + interrupt gate DPL 调整是最小可解释路径，避免引入 `syscall`/`sysret` 的 MSR、`swapgs` 和段布局复杂性。

替代方案：直接实现 `syscall`/`sysret`。否决理由：需要 STAR/LSTAR/FMASK、用户/内核段排列、内核栈切换和 `swapgs` 策略，不适合作为首个用户程序 change。

### Decision: `write` / `exit` 是最小 syscall 闭环

user entry and syscall capability 将诊断 syscall 扩展为用户态可用的最小接口：

- `write`：接受用户 buffer 与长度，复制或逐段校验后输出到 console/serial bounded marker 路径；非法用户指针返回错误或终止进程。
- `exit`：记录当前进程 exit code，标记进程 terminated，并切回内核调度/idle 路径，不返回用户态。

理由：`write` 证明用户到内核的参数传递和用户内存读取，`exit` 证明用户态能受控结束。两者足以形成 smoke，不需要完整 fd/VFS。

替代方案：只保留 `SYS_DEBUG_WRITE`。否决理由：无法固定用户指针校验和进程退出语义，user entry and syscall capability 的闭环证据不足。

### Decision: 用户态错误以 kill/diagnostic 结束，不做恢复

`#PF` handler 保持诊断优先，但需要识别 CPL3 fault 并记录 `BIGOS_USER_PAGE_FAULT` 或等价 marker。user entry and syscall capability 对用户态页错误、非法 syscall 指针或不可执行映射不做 demand paging 恢复，处理结果为终止当前用户进程或 panic/halt，具体取决于是否已有可安全返回的调度上下文。

理由：首个用户程序阶段只需要安全失败和可观测诊断，不应引入 demand paging、signal 或异常投递。

替代方案：直接 panic 所有 `#PF`。该方案简单但会掩盖“用户 fault 不应破坏内核”的边界；user entry and syscall capability 至少应区分 CPL3 fault 与内核 fault。

## Risks / Trade-offs

- [Risk] GDT/TSS/RSP0 配置错误导致 CPL3 syscall 或 exception 使用错误栈 -> Mitigation: 源码级检查用户 selector、TSS/RSP0 wiring，并用最小 ring3 smoke 验证 syscall/exit 返回路径。
- [Risk] 用户页表激活后内核高半区不可达 -> Mitigation: 复用ring0 syscall diagnostic capability 高半区复制不变量，增加 CR3 激活前后的 kernel text/direct map/KVMEM 可达性检查。
- [Risk] 用户指针校验不完整导致内核读取非法地址 -> Mitigation: `write` 只接受 bounded 长度，先提供显式 user range + present/user bit 检查，非法输入返回错误或终止进程。
- [Risk] `int 0x80` gate DPL 放宽扩大内核入口面 -> Mitigation: 仅 syscall vector DPL=3，其他 exception/IRQ vector 保持既有 DPL/EOI 语义，dispatcher 对未知 number 返回错误。
- [Risk] `exit` 后当前执行栈与进程/线程生命周期交错 -> Mitigation: user entry and syscall capability 不立即回收当前内核栈或进程对象，延后到 bounded terminated state。
- [Risk] Bochs serial/VGA oracle 不稳定导致 runtime smoke 不能稳定观测 -> Mitigation: 保留源码级与构建验证为硬门槛，runtime smoke 不可用时记录命令、失败点和 bootability 风险。

## Migration Plan

1. 新增用户程序构建产物与链接/嵌入方式，保持默认构建不运行 smoke。
2. 新增 `proc` 最小进程对象、用户 ELF/blob loader、用户栈映射和地址空间激活 API。
3. 新增或扩展 x86_64 GDT/TSS/user selector 初始化，配置 `VECTOR_SYSCALL` 为允许 CPL3 触发的受控 gate。
4. 实现 `iretq` ring3 进入 helper，接入默认关闭 `user_program_smoke`。
5. 扩展 syscall dispatch：用户态 `write`/`exit`、用户指针检查、未知 number 错误返回。
6. 扩展 `#PF` / fault 诊断：区分 CPL3 fault 与内核 fault，输出稳定 `BIGOS_USER_*` marker。
7. 补源码级测试、文档和 OpenSpec validation；可用时运行 Bochs marker smoke。

回滚策略：`user_program_smoke` 默认关闭，ring3 运行路径可从 `kernel()` smoke wiring 中移除；保留ring0 syscall diagnostic capability syscall 与用户地址空间准备能力不变。若 GDT/TSS 或 IDT DPL 调整影响默认 boot，应先恢复默认 DPL=0 和不进入 `run_user_process()` 的初始化路径。

## Open Questions

- 首个用户程序采用 ELF64 解析还是更小的 flat blob？实现阶段可按当前构建复杂度选择，但 spec 必须要求代码/数据/栈权限边界可验证。
- `write` 是否引入 fd 参数？user entry and syscall capability 可接受固定 console sink 或只支持 fd=1，完整 fd table 留给文件系统/进程后续 change。
- 用户态 fault 后应返回 idle 还是 panic/halt？若调度器已有安全当前线程上下文，优先 terminated + idle；否则记录 marker 后 halt。
