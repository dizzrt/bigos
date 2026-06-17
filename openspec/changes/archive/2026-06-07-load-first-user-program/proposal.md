## Why

ring0 syscall diagnostic capability 已完成用户地址空间页表准备与 `int 0x80` syscall 入口，BigOS 已具备进入用户态前的两块关键边界：可派生低半区独立的用户页表根，以及可由软件触发的受控内核入口。下一步需要把这些基础串成最小闭环：构造一个用户进程，加载第一个内置用户程序，切换到 ring3，并通过 `write`/`exit` 类最小 syscall 回到内核。

当前内核仍停留在 ring0 内核线程模型；ring0 syscall diagnostic capability 的 specs 明确禁止切 CR3、进入 ring3 或加载用户程序。user entry and syscall capability 需要用一个专门 change 放宽这些阶段性禁令，同时保持 boot、内存、中断、调度和诊断 ABI 稳定。

## What Changes

- 新增最小进程 / 用户任务抽象：进程持有用户地址空间根、用户入口、用户栈、关联内核线程/内核栈和退出状态；仍限定单核、无 SMP、无多进程公平性目标。
- 新增第一个用户程序加载路径：先使用内核内嵌或构建期打包的最小用户 ELF / 等价镜像作为输入，映射用户代码、只读数据、数据/BSS 和用户栈到独立低半区地址空间。
- 新增 ring3 进入路径：建立必要的 x86_64 用户段 / TSS 或等价栈切换前置，构造 `iretq` 进入帧，从内核线程上下文切到 CPL3 用户入口。
- 扩展 syscall 使用边界：ring0 syscall diagnostic capability 已定义的 `int 0x80` ABI 需要允许从 ring3 触发，`SYS_DEBUG_WRITE`/`SYS_GET_TICK` 之外补齐最小 `write`/`exit` 语义或将诊断 syscall 收敛为该闭环可用的最小 ABI。
- 扩展用户地址空间使用边界：允许在受控进程运行路径中切换到派生页表根，并在返回内核路径恢复或保持可访问的内核高半区映射。
- 扩展 `#PF` 用户态错误处理边界：对 CPL3 页错误、非法用户地址或缺页场景给出受控 kill/panic 诊断路径，不实现 demand paging 或恢复。
- 新增默认关闭的 `user_program_smoke` 或等价构建开关，输出确定性 `BIGOS_USER_*` marker，用源码级检查、构建和可选 Bochs smoke 验证用户程序启动、syscall write、exit 和错误路径。

非目标：

- 不实现 `fork`/`exec`/`wait`、信号、shell、文件描述符表、VFS 或 POSIX 完整 syscall 表。
- 不实现 demand paging、copy-on-write、用户态动态链接、用户堆或 `mmap`。
- 不实现抢占调度、多进程公平性、SMP、per-CPU run queue 或跨 CPU 地址空间切换。
- 不实现块设备/文件系统加载用户程序；首个用户程序可来自内核内嵌镜像或构建期打包产物。
- 不改动 boot 固定地址、kernel higher-half base、kernel load base、BootInfo handoff ABI、direct map、`KVMEM_BASE` heap/vmalloc 语义或页表 self-mapping 地址。

## Capabilities

### New Capabilities

- `first-user-program`: 最小进程模型、用户程序加载、用户页表激活、ring3 进入/返回、最小 `write`/`exit` syscall 闭环、用户态错误处理和验证要求。

### Modified Capabilities

- `user-address-space-vmem`: 将ring0 syscall diagnostic capability 的“派生用户页表根但不得切 CR3/进入 ring3”约束扩展为“默认准备 API 不切换，但user entry and syscall capability 进程运行路径 MAY 激活派生根并运行用户映射”，并固定激活/恢复边界。
- `syscall-entry`: 将ring0 syscall diagnostic capability 的“仅 ring0 自测、IDT gate DPL=0、不从用户态触发 syscall”扩展为允许 ring3 通过受控 `int 0x80` 入口进入内核，并补充用户指针、长度、返回值和 `exit` 语义。

## Impact

- 受影响子系统：`kernel/core/sched` 或等价调度路径、`kernel/core/irq` 的 IDT/syscall/#PF 处理、`kernel/core/sys` syscall dispatch、`kernel/mm/vmem` 用户地址空间 primitive、`kernel/core/kernel.cc` 初始化与 smoke wiring、`xmake.lua` 构建开关、`tests` 源码级检查和 `docs/en/arch` 架构文档。
- 架构假设：x86_64、单核、Legacy BIOS/i8259/PIT、kernel-owned 静态 IDT、当前 GDT 可扩展为用户段/TSS 所需布局；不引入 SMP 或 APIC。
- 内存假设：kernel higher-half、direct map、KVMEM 和 self-mapping 地址布局保持不变；用户低半区映射独立；用户程序镜像和用户栈均由非中断上下文创建。
- 中断/上下文假设：进入 ring3 前已完成 IDT、syscall gate、TSS/内核栈或等价机制；外部 IRQ EOI 语义不变；syscall path 不是外部 IRQ，不发送 i8259 EOI。
- 工具链 / emulator：继续使用 `xmake`、`x86_64-elf-g++`、`uv run pytest` 和 `openspec validate`；Bochs serial/VGA oracle 不稳定时必须记录实际命令、失败点与剩余 bootability 风险。
