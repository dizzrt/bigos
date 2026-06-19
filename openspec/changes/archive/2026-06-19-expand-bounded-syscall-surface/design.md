## Context

BigOS 当前已有默认 PID-1 init、`/bin/sh`、bounded fd/VFS、`fork`/`execve`/`wait`、信号、cwd、可写 `/rw`、metadata 查询、进程组/session 与默认终端 foreground binding。现有 syscall 面已经可以支撑内置 shell 与项目内小程序，但对更标准的小型 C 程序仍不够友好：等待接口仍以 BigOS raw wrapper 为主，fd close-on-exec 缺少用户态控制面，部分文件/路径状态查询与基础进程 primitive 缺少统一 libc 消费形态。

本 change 只在当前单核 x86_64 Legacy BIOS/MBR/exFAT/bigfs 默认路径上扩展 bounded syscall/libc surface。它不改变 boot handoff、link address、page-table layout、direct map、IDT/syscall vector、CR3 切换、磁盘布局或 UEFI backend 边界。

## Goals / Non-Goals

**Goals:**

- 为 `wait`/`waitpid` 提供有界 POSIX-like wrapper、`WNOHANG` 非阻塞子集与 status 宏，使父进程可以等待任意子进程或指定子进程，并对 unsupported options 得到确定性错误。
- 为 fd table 增加 close-on-exec 查询/设置、`F_DUPFD` 和有界 `fcntl`-like 消费形态，使用户态能显式控制 `execve` 后的 fd 继承与标准形态 fd duplication。
- 将文件/路径状态查询、access 类检查、truncate 类操作和删除/目录错误边界整理为一致的 bounded syscall/libc contract。
- 补齐基础进程信息 primitive 的 libc 消费面，保持 pid/ppid/pgid/sid/uid/gid 与信号权限边界的确定性。
- 提供 source-level 和可用时的 QEMU headless 验证，覆盖成功路径、错误码、用户指针 copy、fd 生命周期和 `execve` 继承边界。

**Non-Goals:**

- 不实现完整 POSIX `waitpid` 选项、`waitid`、resource usage、stopped/continued 状态、orphaned process group 规则或完整 job control。
- 不实现完整 `fcntl`、record locking、nonblocking I/O、async I/O、`poll`/`select`、socket、mount namespace、symlink、`chroot`、权限/credential 完整模型或完整 POSIX filesystem。
- 不引入动态链接、共享库、完整 POSIX libc、SMP、跨核 wait/signal、UEFI runtime parity、新 ISA/backend 或 broad storage/device 支持。
- 不让 IRQ、preemption-disabled 或 scheduler critical path 执行会阻塞、分配、访问用户指针或同步存储 I/O 的 syscall 逻辑。

## Decisions

1. **把新增能力分成 syscall contract 与 libc wrapper 两层。**

   - 理由：内核 syscall 层需要保持负 errno、寄存器 ABI、用户指针验证和生命周期约束；libc 层则负责提供更接近小程序预期的 `errno`、status 宏和 wrapper 名称。分层可以避免把 POSIX 命名误读为完整内核语义。
   - 替代方案：只新增内核 syscall，不提供 libc wrapper。该方案降低实现量，但用户程序仍需要 BigOS 专用 raw 调用，不能达成 portable small programs 的目标。

2. **`waitpid` 作为现有 wait 核心的有界扩展，而不是引入完整 wait 状态机。**

   - 理由：当前进程模型已有 parent-child、zombie/reap 和 deterministic exit status。支持 `pid > 0`、`WAIT_ANY` 与 `WNOHANG` 的非阻塞有界子集足以覆盖 shell、小程序和验证需求；其他 pid 形态、stopped/continued 状态会拉入 job-control 和 scheduler 语义，超出边界。
   - `WNOHANG` 边界：只表示“当前没有匹配的已退出子进程时立即返回 0”，不得订阅异步通知、不得观察 stopped/continued 状态、不得改变未退出子进程状态；若已有匹配 zombie，仍按普通 `waitpid` 路径 reap 并返回 child pid。
   - 替代方案：一次性实现完整 POSIX wait family。该方案需要停止/继续状态、process group wait、resource usage 和更复杂信号交互，不适合当前阶段。

3. **close-on-exec 和 `F_DUPFD` 作为有界 `fcntl`-like 接口暴露。**

   - 理由：fd table 已归属进程并参与 `execve`/reap 生命周期；把 close-on-exec 放在 entry 上能在 exec commit 时统一关闭，且不影响 open file object 的共享 offset 与引用计数语义。`F_DUPFD` 可复用现有 lowest-available descriptor allocation 与 open file reference 规则，为小程序提供标准形态的 fd duplication，而不引入 `dup3`、nonblocking 或 locking 语义。
   - `F_DUPFD` 边界：只支持从调用方给出的最小 fd 编号开始分配最低可用 descriptor，复制 open file object 引用与 offset 共享关系，新 fd 的 close-on-exec flag 默认清除；非法起点、bad fd 或 fd table 容量不足返回确定性 errno。
   - 替代方案：只暴露 `F_GETFD`/`F_SETFD` 或新增 `dup3`。前者会让小程序仍需要 BigOS 专用 duplication 形态；后者会扩大 fd API 面并引入 flags 组合语义，不适合本 change。

4. **metadata/access/truncate 复用 VFS path resolution 与现有 backend status 映射。**

   - 理由：BigOS 已有 cwd/relative path、metadata、可写 `/rw` 和 read-only boot assets。新增用户可见 primitive 应共享同一条有界路径解析、用户路径 copy、backend permission/capacity 和 errno 映射路径，避免并行规则。
   - 替代方案：在 syscall 层按字符串前缀手写路径判断。该方案容易绕过 cwd、backend status 和不可阻塞上下文检查。

5. **新增 syscall 只在普通用户进程 syscall 上下文执行完整逻辑。**

   - 理由：这些 primitive 可能 copy 用户指针、查进程表、访问 fd table、做路径解析或同步 I/O，必须保持可阻塞上下文边界。IRQ 或 scheduler-critical 路径只能观察诊断状态或返回确定性错误。
   - 替代方案：让部分查询在任意上下文可调用。该方案会混淆 kernel internal helper 与用户 syscall 边界，增加不可阻塞路径误用风险。

## Risks / Trade-offs

- [Risk] 新增 wrapper 名称接近 POSIX，容易被误认为完整兼容。
  Mitigation: headers、docs、specs 和 validation notes 必须持续标注 bounded BigOS subset，并对 unsupported flags/options 返回确定性错误。

- [Risk] close-on-exec 与 `dup2`、`fork`、`execve` 的交互可能导致 fd 泄漏或重复关闭。
  Mitigation: 明确 entry flag 继承/清除规则，覆盖 `fork` 继承、`dup` flag 清除或复制策略、`dup2` 覆盖关闭和 exec commit close-all-on-exec 测试。

- [Risk] metadata/access 类 syscall 可能绕过 backend 权限或 cwd 删除状态。
  Mitigation: 所有 path-taking primitive 通过共享 VFS path resolution 与 backend status 映射；相对路径、已删除 cwd、read-only backend 和 `/rw` 权限失败都进入同一错误边界。

- [Risk] wait status 编码如果过度接近 POSIX，会限制后续信号和 job-control 演进。
  Mitigation: 保持 BigOS 有界 status 编码文档化，libc 宏只解释当前支持的 exited/signaled 子集，reserved bits/unsupported states 不承诺 POSIX 完整语义。

## Migration Plan

1. 先审查现有 syscall 编号、libc wrapper 和 tests，确认新增编号追加而不重排既有 ABI。
2. 扩展 wait 核心与 libc wrapper，覆盖 `wait`/`waitpid`、`WNOHANG` 非阻塞返回、普通阻塞等待和 unsupported options。
3. 扩展 fd table close-on-exec flag 与有界 `fcntl`-like 操作，覆盖 `F_GETFD`、`F_SETFD`、`FD_CLOEXEC`、`F_DUPFD`，再接入 `execve` commit 和 `dup`/`dup2` 语义。
4. 复用 VFS path/fd metadata helper，补齐 access、stat/fstat 消费面和 truncate 类 wrapper 的一致错误映射。
5. 更新用户态 headers、小型验证程序、shell 消费点和中英文文档。
6. 运行 source-level 检查、默认构建和可用时 QEMU headless smoke；若 emulator/toolchain 不可用，记录 blocker、替代检查和剩余 runtime 风险。

回滚策略是保留 syscall 编号但让未完成入口返回 `-ENOSYS` 或确定性 unsupported errno，libc wrapper 回退为设置 `errno` 并失败，shell 与现有用户程序继续使用已有 raw wrapper 路径。

## Resolved Boundaries

- 本 change 纳入 `waitpid(..., WNOHANG)` 的 BigOS 有界子集：只支持 `pid > 0` 和 `WAIT_ANY` selector 下的非阻塞 reap 检查，未命中 exited child 时返回 0，其他 wait options 继续确定性失败。
- 本 change 纳入 `F_DUPFD`：它与 `F_GETFD`、`F_SETFD`、`FD_CLOEXEC` 同属有界 `fcntl`-like surface；不实现 `F_DUPFD_CLOEXEC`、record locking、nonblocking、async I/O 或完整 POSIX `fcntl`。
