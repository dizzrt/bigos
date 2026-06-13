## Why

当前 BigOS 已具备 cwd、`cd`、`pwd`、基础 fd/VFS、元数据和有界 shell baseline，但用户从交互式 shell 自然探索路径与目录内容的能力仍然零散。现在应把已有路径与元数据契约收敛成一组小型、有界、用户可见的路径工具，为后续 rename、文件系统语义硬化和 shell 可用性阶段提供可观察基础。

## What Changes

- 增加有界用户态路径工具集合，覆盖目录列举、文件内容查看、元数据观察、目录创建和路径删除等小型静态用户程序或等价 shell 消费路径。
- 让这些工具通过现有 libc wrapper、fd/VFS、cwd、相对路径和元数据契约工作，并在 shell 中以普通命令自然运行。
- 为工具输出、错误报告、退出状态和路径解析行为定义可验证边界，使自动或手工验证能观察路径与元数据 contract。
- 保持范围有界：不引入完整 POSIX utility suite、完整 shell 语言、globbing、脚本环境、符号链接、mount namespace、持久完整可写文件系统、async I/O、SMP 或新 boot/architecture runtime parity。

## Capabilities

### New Capabilities
- `userland-path-tools`: 定义 BigOS 小型用户态路径工具、shell 消费路径、相对路径/元数据可观察行为、错误边界和行为导向验证。

### Modified Capabilities

无。该 change 在新的 `userland-path-tools` 能力中约束一组用户可见消费路径，并复用既有 cwd、fd/VFS、metadata、libc、shell 和用户程序构建契约；若实现阶段发现现有能力的 requirement 需要收紧，再补充对应 delta。

## Impact

- Affected boot/kernel subsystem: normal x86_64 Legacy BIOS runtime path after kernel init, process/syscall/fs/userland subsystems; no bootloader, linker address, interrupt vector, disk layout, page-table layout, CR3 switching, storage-driver, or boot image discovery contract changes are intended.
- Affected kernel/userland areas: `kernel/core/fs` path lookup and directory/metadata behavior as consumed by tools, `kernel/core/proc` process/fd inheritance only through existing contracts, `kernel/core/syscall` path-taking ABI wrappers, `include/bigos` public ABI headers, `user/libc`, `/bin/sh`, and packaged `user/bin/*` programs.
- Architecture and memory assumptions: current runnable backend remains single-core x86_64 with the existing virtual memory layout, bounded user-buffer validation, bounded path buffers, and freestanding-safe user programs.
- Disk and emulator assumptions: tools are packaged into the existing raw image paths without changing MBR/partition/exFAT discovery. Runtime validation depends on configured `x86_64-elf-*`, xmake, QEMU/Bochs, ROM/display setup, disk image path, and timeout/serial oracle availability.
