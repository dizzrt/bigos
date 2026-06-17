## Why

BigOS 已完成常规进程生命周期、bounded ELF64 `exec`、blocking primitives 和只读 exFAT 读取路径；继续推进 VMA、demand paging 或 userland runtime 前，需要先稳定 kernel/user I/O 边界。

fd/VFS shell boundary 聚焦最小 fd/VFS 壳层：把现有只读 exFAT 能力挂到 vnode/file/file table 抽象后面，并向用户态暴露受控的 `open`/`read`/`close` 语义，避免后续 userland 直接依赖 exFAT 私有 API。

## What Changes

- 引入最小 VFS 壳层：定义 vnode、open file、file operations、mount/root vnode 和只读 exFAT backend adapter。
- 引入进程文件描述符表：为每个 process 提供有界 fd table、fd allocation、close-on-exec 或继承策略、引用计数与安全回收规则。
- 增加用户态 I/O syscall：在现有 `int 0x80` ABI 下增加 `open`、`read`、`close`，并复用用户指针/长度校验与 bounded copy 规则。
- 复用现有只读存储栈：VFS read 路径底层仍使用 ATA PIO block device、MBR exFAT discovery、read-only exFAT lookup/read，不引入写入或 page cache。
- 连接 `exec` 边界：定义 `exec` 时 fd table 的最小继承规则，使 future userland 能在进程生命周期内稳定使用 fd。
- 保留阶段边界：不实现 writable filesystem、directory mutation、permissions、page cache、async I/O、`dup`/`pipe`/`select`、`fork` fd 复制、VMA/demand paging 或 user-space libc。

## Capabilities

### New Capabilities

- `fd-vfs-shell`: 定义 BigOS 最小 vnode/file/file-table/VFS 壳层、进程 fd table、`open`/`read`/`close` syscall、只读 exFAT backend 接入、fd 生命周期和验证语义。

### Modified Capabilities

- `syscall-entry`: 扩展用户可见 syscall 集合，增加 `open`、`read`、`close` 的 ABI 参数、错误返回和用户 buffer 安全要求。
- `process-lifecycle`: 扩展 process ownership，使进程对象持有 fd table，并定义 `exec` 时 fd 继承/关闭与进程退出时 fd 回收规则。

## Impact

- 受影响子系统：`kernel/core/fs`、新增或调整的 VFS/fd headers、`kernel/core/proc`、`include/bigos/proc.h`、`kernel/core/syscall`、`include/bigos/syscall.h`、现有 exFAT 读取路径、`xmake.lua`、runtime smoke matrix、source-level tests 和相关文档。
- 架构假设：仅 x86_64 单核 Legacy BIOS/MBR/exFAT 路径；保留 `int 0x80` syscall vector、`InterruptFrame` ABI、GDT/TSS/RSP0、kernel higher-half、direct map、KVMEM 和 recursive self-mapping 常量。
- 内存与阻塞假设：fd/VFS 操作只能在允许阻塞的进程或普通内核上下文执行；不得在 IRQ、preemption-disabled scheduler critical section 或 panic path 中分配 fd/file/vnode 对象或执行可能阻塞的磁盘读取。
- 磁盘/文件系统假设：第一版只挂载当前 raw image 内的 MBR/exFAT 只读卷；路径 lookup 和 file read 仍受现有 exFAT bounds 限制；不改变 ATA PIO 同步读、MBR partition discovery 或 exFAT on-disk 支持范围。
- emulator 与工具链假设：继续使用 `xmake`、`x86_64-elf-*`、QEMU headless serial-marker smoke；涉及 ATA PIO、IRQ/timer/blocking、port-IO 或 user syscall 行为时，在可用环境下保留 Bochs 或 QEMU+Bochs 交叉验证。
- 非目标：不实现写入、目录创建/删除/rename、权限/uid/gid、mount namespace、page cache、async I/O、`mmap`、`brk`、demand paging、COW、`fork` fd 复制、signals、user libc、dynamic linker、SMP、UEFI backend 或 CI release automation。
