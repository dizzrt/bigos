## Why

BigOS 已具备 `/rw` 运行时可写文件系统、cwd/相对路径、libc 文件 wrapper、shell 和用户态路径工具，但缺少一个可观察的路径重命名闭环。现在引入受限 rename，可以为后续文件系统语义硬化阶段提供更完整的目录项变更基础，同时继续避免过早承诺完整 POSIX rename。

## What Changes

- 增加有界 rename 能力，覆盖 `/rw` 运行时可写后端内的常规文件重命名和简单路径搬移。
- 将 rename 通过 fd/VFS、syscall、用户态 libc wrapper 和小型用户态工具作为一个一致能力暴露。
- 定义保守失败语义：只读后端、跨挂载、缺失父目录、目标已存在且不允许替换、目录目标、不支持对象、权限拒绝、容量/IO 失败和非法用户路径都必须确定性失败。
- 保持范围有界：不引入硬链接、符号链接、跨挂载 rename、完整目录 rename、完整 POSIX atomic replacement、持久完整可写文件系统、async I/O、SMP 或新 boot/architecture runtime parity。

## Capabilities

### New Capabilities

无。该 change 受控扩展现有文件系统、syscall、libc 和用户态工具能力，不新增独立能力族。

### Modified Capabilities

- `writable-filesystem`: 将 `/rw` 运行时可写后端从“不支持 rename”扩展为支持保守、有界的常规文件 rename。
- `fd-vfs-shell`: 将路径型 VFS 操作扩展为包含受限 rename，并保持 cwd/相对路径、阻塞上下文和错误映射边界一致。
- `syscall-entry`: 以 append-only 方式暴露 rename syscall 或等价用户 ABI 入口，保持既有 syscall vector、寄存器 ABI、号位稳定性和 no-EOI 语义。
- `user-libc-min`: 为简单静态 C 程序声明并实现 rename wrapper，沿用 errno 翻译与 freestanding 头文件边界。
- `userland-path-tools`: 增加可从 shell 观察 rename 行为的小型工具或等价用户态消费路径。

## Impact

- Affected boot/kernel subsystem: normal x86_64 Legacy BIOS runtime path after kernel init, especially process/syscall/fs/userland subsystems. No bootloader, linker address, interrupt vector, page-table layout, CR3 switching, raw disk image discovery, ATA PIO, MBR, partition, or exFAT read contract changes are intended.
- Affected kernel/userland areas: `kernel/core/fs` VFS and writable backend directory-entry mutation, `kernel/core/syscall` path-taking syscall dispatch, `kernel/core/proc` only through existing process/fd/current-directory context, `include/bigos` ABI headers, `user/libc`, `/bin/sh` consumption, and packaged `user/bin/*` tools.
- Architecture and memory assumptions: current runnable backend remains single-core x86_64; user paths and buffers stay bounded and VMA-validated; rename must not require new page-table, linker, GDT/TSS, or address-layout assumptions.
- Disk and emulator assumptions: `/rw` remains RAM-backed with runtime consistency only; read-only boot assets stay immutable; validation depends on configured `x86_64-elf-*`, xmake, QEMU/Bochs, ROM/display setup, disk image path, timeout controls, and serial/log observability.
