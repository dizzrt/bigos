## Why

BigOS 已具备有界 fd/VFS、只读 exFAT、RAM-backed `/rw`、最小 libc、shell 与小型用户程序基线，但简单程序仍缺少统一的文件和目录元数据查询契约。现在补齐最小 `stat`/`fstat` 风格能力，可以让内核、libc、shell/用户工具和行为验证闭合一个可观察的 kernel-to-userland 能力环。

## What Changes

- 增加有界文件与目录元数据契约，覆盖常规文件、目录、只读 exFAT 后端与 RAM-backed `/rw` 后端的最小可观察字段。
- 在内核 syscall/fd/VFS 路径中暴露 `stat`/`fstat` 风格查询，统一绝对路径查询与已打开 fd 查询的成功、失败和用户缓冲校验语义。
- 在 freestanding 用户态 libc 中增加对应 wrapper、公共类型/常量和 errno 翻译，使简单静态 C 程序不需要直接解释内核负 errno。
- 增加小型 shell/用户程序消费路径，用于从交互式 shell 或打包用户程序观察文件类型、大小、基础 mode/owner 等有界字段。
- 增加行为导向验证，覆盖成功查询、缺失路径、非法 fd、只读/可写后端差异、目录与常规文件元数据，以及用户缓冲失败。
- 保持范围有界：不引入符号链接、设备节点模型、完整 POSIX metadata database、扩展属性、ACL、完整权限数据库、文件时间戳语义或广泛标准兼容声明。

## Capabilities

### New Capabilities
- `file-metadata-contract`: 定义 BigOS 最小文件与目录元数据契约，包括内核 fd/VFS 查询、libc 暴露、用户态消费和行为验证。

### Modified Capabilities
- 无。既有 `fd-vfs-shell`、`writable-filesystem`、`user-libc-min` 和 `user-shell` 能力作为消费基础保持其既有需求边界；本 change 通过新的元数据 capability 补充跨层契约。

## Impact

- 受影响子系统：`kernel/core/fs` 的 VFS/后端元数据查询路径、`kernel/core/syscall` 的用户 ABI 暴露、`kernel/core/proc` 的 fd 和用户缓冲边界、`user` 下的 libc 与小型用户程序、`/bin/sh` 可选消费路径、行为验证测试。
- 架构假设：当前可运行 backend 仍为 x86_64 Legacy BIOS/MBR/exFAT；新增契约不得要求 UEFI、第二 ISA、SMP 或新的 boot/storage runtime parity。
- 内存与 ABI 假设：用户缓冲复制继续使用现有 VMA-backed 校验边界；公共结构体布局必须显式有界并适配当前 x86_64 用户 ABI，不改变既有 syscall 向量或寄存器约定。
- 磁盘与文件系统假设：只读 exFAT 元数据来自现有只读后端，`/rw` 元数据来自 RAM-backed 可写后端；不改变磁盘镜像、MBR、分区、exFAT 只读发现或跨重启持久化边界。
- 工具链与验证假设：实现验证优先使用源码/行为断言和默认关闭的运行时 smoke；QEMU、Bochs、交叉工具链、ROM/display 或镜像不可用时记录为跳过而非声称通过。
