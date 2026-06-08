## Why

BigOS 已完成 smoke 级 ring3 用户程序、filesystem-backed ELF loader、blocking primitives 和 scheduler semantics；继续推进 fd/VFS、VMA/demand paging 或更完整 userland 前，需要先把当前仅在 smoke 配置下编译的 `src/kernel/proc` 提升为可复用的常规内核子系统。

阶段 12 聚焦稳定进程生命周期边界：明确 PID、进程表、父子关系、`wait`/`exit` 和 general `exec argv/envp` 的最小语义，同时保留当前单核、同步、read-only filesystem 和 safe CR3/root teardown 约束。

## What Changes

- 引入常规进程核心：让 process core 可在非 smoke 配置下构建，并提供独立于具体 smoke case 的 `Process` lifecycle API。
- 增加 PID 与进程表策略：定义 PID 分配、进程记录状态、父子关系、zombie/reap 生命周期、进程表容量和失败行为。
- 增加 `wait`/`exit` 语义：将用户态退出、fault termination、父进程等待和资源回收连接到现有 blocking/wait queue 与 safe reaper 边界。
- 增加 general `exec` 路径：将现有 bounded ELF64 `ET_EXEC` loader 提升为可由进程生命周期调用的 exec primitive，并支持基础 `argv`/`envp` 栈布置。
- 保持资源 ownership：继续保护 CR3/root、user page、dynamic PT/PD/PDPT、process kernel stack 和 loader 临时资源的所有权与 teardown 顺序。
- 保留阶段边界：不实现 `fork`、COW、signals、broad POSIX policy、fd/VFS、demand paging、user libc、SMP 或 writable filesystem。

## Capabilities

### New Capabilities

- `process-lifecycle`: 定义常规进程核心、PID/进程表、父子关系、`wait`/`exit`、general `exec argv/envp`、zombie/reap 和安全资源回收语义。

### Modified Capabilities

- `first-user-program`: 将原本 smoke-only 的最小用户进程边界改为复用常规 process lifecycle，同时保留 embedded flat smoke 的默认关闭和 filesystem-independent 特性。
- `user-elf-program-loader`: 将 bounded ELF64 smoke loader 的 runtime binding 改为可被 general `exec` 路径复用，并增加基础 `argv`/`envp` 输入约束。
- `address-space-lifecycle`: 将 safe teardown 需求扩展到 PID/进程表/zombie/reap 生命周期，明确 `wait`/`exit` 与地址空间释放的安全上下文关系。

## Impact

- 受影响子系统：`src/kernel/proc`、`include/bigos/proc.h`、`src/kernel/syscall`、`include/bigos/syscall.h`、`src/kernel/sched`、`include/bigos/sched.h`、`src/mm` 用户地址空间 teardown、`src/kernel/fs` 只读 ELF 读取路径、`xmake.lua` smoke/normal build gating、runtime smoke matrix 和相关测试/文档。
- 架构假设：仅 x86_64 单核 Legacy BIOS/MBR/exFAT 路径；保留 `int 0x80` syscall ABI、GDT/TSS/RSP0、`iretq` ring3 entry、kernel higher-half、direct map、KVMEM 和 recursive self-mapping 常量。
- 内存假设：process lifecycle 只能在安全非 IRQ 上下文分配/释放 process 对象、kernel stack、user pages 和页表；退出/fault/syscall 当前路径不得释放 active kernel stack 或 active CR3 root。
- emulator 与工具链假设：继续使用 `xmake`、`x86_64-elf-*`、QEMU headless serial-marker smoke；涉及 ring3、syscall、timer/blocking、ATA PIO 或 port-IO 行为时，在可用环境下保留 Bochs 或 QEMU+Bochs 交叉验证。
- 磁盘/文件系统假设：继续复用 Legacy raw image、MBR/exFAT、read-only ATA PIO/exFAT 和 `/boot/user/init.elf`；本阶段不引入 writable filesystem、VFS、page cache、async I/O 或文件描述符继承。
- 非目标：不实现 `fork`、COW、signals、complete POSIX process model、fd/VFS、`open`/`read`/`close` 用户 API、demand paging、`mmap`/`brk`、user-space libc、dynamic linking、SMP、UEFI backend 或 CI release automation。
