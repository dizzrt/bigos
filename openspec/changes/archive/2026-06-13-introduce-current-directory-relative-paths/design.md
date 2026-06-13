## Context

BigOS 已具备默认 PID-1 init、`/bin/sh`、fd/VFS、`/rw`、元数据查询、`fork`/`execve`、最小 libc 和行为导向验证 baseline，但路径入口仍主要以绝对路径为可用边界。这让简单程序、shell 重定向、元数据工具和后续路径工具必须显式传递完整路径，阻碍自然的用户态文件操作。

本变更跨越进程状态、VFS 路径解析、syscall 用户缓冲、libc wrapper、shell 内建命令和验证路径。它不触碰 bootloader、链接地址、IDT/syscall vector、页表布局、磁盘镜像布局或新存储驱动。

## Goals / Non-Goals

**Goals:**

- 为每个进程提供一个有界 cwd 状态，默认从 root 开始，并在 `fork` 与 `execve` 中保持符合直觉的继承。
- 让路径型内核接口统一接受绝对路径和相对路径，并在 VFS 层使用同一解析入口。
- 向用户态暴露 `chdir`/`getcwd` 与相对路径可用的 libc wrapper。
- 让 shell 以自身进程状态消费 cwd，提供有界 `cd` 行为，并让命令、重定向和工具路径共享相同解析语义。
- 提供 source/build/runtime 分层验证，覆盖成功路径、失败路径、继承语义和环境不可用记录。

**Non-Goals:**

- 不实现 mount namespace、`chroot`、符号链接遍历、硬链接语义或完整 POSIX pathname canonicalization。
- 不实现 symlink-aware `..`、mount namespace 根边界、`chroot` 根边界或 deleted-cwd/rename 后目录身份追踪；但第一版明确支持当前目录树内 POSIX-style `..` 组件语义。
- 不引入持久完整可写文件系统、async I/O、SMP、完整 POSIX shell、完整 POSIX libc 或动态链接。
- 不改变 x86_64 Legacy BIOS 当前可运行 backend、磁盘布局、boot ABI、syscall vector 或地址空间布局。

## Decisions

- **cwd 存储为进程拥有的有界绝对路径字符串。** 选择字符串而不是 vnode 引用，是因为当前 VFS 和 `/rw` lifecycle 更适合先保持简单、可复制、可回滚的状态；代价是目录 rename 等后续语义不会自动跟踪，且本阶段不实现 rename/cwd vnode 稳定性。备选方案是 cwd 持有 vnode 引用，但会提前引入目录对象引用、unlink 后 cwd、rename 后 cwd 等复杂生命周期。
- **VFS 提供统一 path normalization/resolution helper。** syscall 层只负责复制和长度校验用户路径，VFS/proc 边界负责将相对路径与当前进程 cwd 合成有界绝对路径并路由后端。这样可避免每个 syscall wrapper 自行拼接路径。备选方案是让 libc 或 shell 拼接路径，但会让 kernel 与用户态行为不一致，且无法保护直接 syscall 调用者。
- **`fork` 复制 cwd，`execve` 保留 cwd。** cwd 属于进程上下文而不是映像内容；`fork` 子进程从父进程继承当前目录，`execve` 替换用户镜像但不改变 cwd。备选方案是 exec 重置 root，但会破坏常见的 shell 和程序组合模型。
- **`chdir` 只接受已存在目录并在 commit 前完成验证。** 内核先解析目标并确认目录类型，再原子更新当前进程 cwd 字符串；失败不改变旧 cwd。备选方案是先更新再校验，但会让错误路径破坏进程状态。
- **第一版显式支持 POSIX-style `.`/`..` 组件语义。** `.` 保持当前目录，`..` 返回父目录，root 的父目录仍为 root；相对路径中的普通组件、`.` 与 `..` 在统一 helper 中归约，并要求中间组件为目录。该语义覆盖 BigOS 当前支持的目录树，不引入 symlink、mount namespace、`chroot` 或完整 POSIX `realpath`。
- **`getcwd` 缓冲区过小时使用 `ERANGE`。** 选择新增/使用 `ERANGE` 区分“用户缓冲有效但容量不足”和 `EFAULT` 的无效用户缓冲、`EINVAL` 的非法参数；libc wrapper 将内核 `-ERANGE` 翻译为用户态 `errno = ERANGE`。
- **本阶段添加小型 `/bin/pwd` 用户工具。** `/bin/pwd` 作为静态 freestanding 用户程序调用 `getcwd` 并输出当前 cwd，用于手工体验和 runtime validation；暂不要求 shell prompt 展示 cwd，也不要求 `pwd` builtin。

## Risks / Trade-offs

- [Risk] 字符串 cwd 无法表达目录被删除或未来 rename 后的打开目录身份 -> Mitigation: 本阶段明确不承诺 rename、symlink、稳定 inode 或 deleted cwd 语义，后续如需扩展再引入 vnode/refcount 设计。
- [Risk] 多入口路径解析可能出现绝对/相对行为不一致 -> Mitigation: 所有 path-taking syscall 通过同一个 VFS 解析 helper，并用行为验证覆盖 open/stat/exec/redirection。
- [Risk] cwd 更新涉及分配失败和用户缓冲失败 -> Mitigation: 所有新路径字符串先在 staging buffer 中构建，成功验证后再 commit；失败返回确定性 errno 且不修改旧状态。
- [Risk] shell `cd` 在子进程中执行会丢失 cwd 变更 -> Mitigation: `cd` 必须作为 shell 内建在 shell 进程中执行，不经 `fork`/`execve`。
- [Risk] POSIX-style `..` 会被误解为完整 POSIX canonicalization -> Mitigation: 规格只承诺 BigOS 当前目录树内的 `.`/`..` 组件语义，并继续排除 symlink、mount namespace、`chroot` 和 `realpath`。
- [Risk] 环境缺少交叉工具链或 emulator 时无法完成 runtime smoke -> Mitigation: 分层记录已执行 source/build 检查、跳过的 QEMU/Bochs 条件和剩余 bootability 风险。

## Migration Plan

- 先引入规格和公共契约，保持绝对路径行为作为兼容基线。
- 在进程创建路径初始化 cwd 为 `/`，在 `fork`/`execve`/reap 中接入复制、保留和释放规则。
- 用统一 VFS helper 替换各路径入口的绝对路径-only 分支，加入 POSIX-style `.`/`..` 组件归约，并保留对不支持路径形式的确定性错误。
- 添加 libc wrapper、`ERANGE` errno 暴露、头文件声明、shell `cd` 内建和 `/bin/pwd` 最小用户态消费路径。
- 增加行为验证后再更新文档；若实现中发现回归，可回滚到绝对路径-only 入口并保留 specs 中未完成任务。

## Resolved Questions

- 第一版支持 BigOS 当前目录树内的 POSIX-style `..` 组件语义，root 的父目录保持 root。
- `getcwd` 用户缓冲区过小时返回 `ERANGE`，无效用户缓冲仍返回用户内存相关错误。
- 本阶段增加小型 `/bin/pwd` 用户工具，用于用户可见消费和行为验证；暂不要求 shell prompt 展示 cwd 或内建 `pwd`。
