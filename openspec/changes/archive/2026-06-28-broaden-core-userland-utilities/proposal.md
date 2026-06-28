## Why

BigOS 已具备默认 PID-1、`/bin/sh`、fd/VFS、cwd、portable libc subset 与一批用户态工具的基础，但核心工具集合仍需要形成可规格化、可打包、可验证的有界能力，才能支撑更像可用系统的日常用户态操作。

本变更将把核心用户态工具从零散程序推进为一组明确边界的 bounded utilities：面向文件、目录、文本、进程/时间与 shell 组合场景，复用现有 freestanding 用户态和内核契约，而不扩大 BigOS 的 POSIX、coreutils 或 hosted libc 承诺。

## What Changes

- 定义一组有界核心用户态工具的支持边界，覆盖常用只读、写入、路径、目录、文本处理、元数据、时间/进程辅助与 shell 组合工具。
- 使新增或成熟化的工具统一通过现有 freestanding user libc、静态用户程序构建、`/bin` 打包、`execve`、fd/VFS、cwd、pipe/redirection 与 shell 路径消费。
- 为工具行为建立确定性输出、错误报告、退出状态和资源上限要求，避免依赖宿主 libc、动态 loader、线程、locale、宽字符或完整 POSIX 行为。
- 扩展用户态验证，使代表性工具在 `/boot` 只读资产、`/rw` 运行期可写目录、相对路径、pipe/redirection、文本处理和失败路径上可观察。
- 保持非目标清晰：不实现完整 GNU/POSIX coreutils、不引入完整 shell 语法或 job control、不扩大文件系统/权限/终端/locale/Unicode/正则/排序语义、不新增 syscall 或磁盘布局承诺，除非实现过程中发现现有接口存在必须修复的缺陷。

## Capabilities

### New Capabilities

- `bounded-core-userland-utilities`: 定义并验证 BigOS 有界核心用户态工具集合，包括工具选择边界、构建与打包、shell 可见执行、确定性 I/O/错误/退出语义、组合场景和非目标。

### Modified Capabilities

- 无。该变更预期复用现有 `portable-libc-subset`、`bounded-file-streams`、`posix-like-process-io-subset`、`runtime-filesystem-maturity`、`minimal-socket-interface` 等能力；若实现时发现需要改变这些能力的 REQUIREMENTS，应另行补充对应 delta spec。

## Impact

- 受影响子系统：`user/bin` 工具程序、`user/libc` 已有 wrapper/stdio 消费路径、`user/sh` 对 `/bin` 工具的组合消费、用户程序构建与打包规则、`user/smoke` 行为验证。
- 内核交互面：仅消费既有 `execve`、fd/VFS、cwd、pipe/dup/redirection、time/identity、signal/exit/wait 与 socket 等已规格化 syscall/libc wrapper；本提案不主动要求新增 syscall、改变 ABI、改变 IDT/syscall vector、改变 CR3/用户地址空间布局或改变内核内存布局。
- 启动与磁盘假设：默认 x86_64 Legacy BIOS/MBR/exFAT 打包路径继续提供 `/boot/user/init.elf`、`/bin/sh` 和 `/bin/*`；UEFI backend parity、磁盘分区布局变更和跨重启持久语义不在本变更范围。
- 工具链与运行假设：使用 xmake 与 `x86_64-elf-gcc` freestanding 用户程序构建路径；运行时验证优先使用 QEMU headless helper，Bochs 可作为早期启动或硬件行为交叉验证补充。
- 文档影响：需要同步更新 `docs/en` 与 `docs/zh` 中的用户态能力说明，继续把 BigOS 描述为 bounded userland，而不是完整 POSIX 或通用发行版环境。
