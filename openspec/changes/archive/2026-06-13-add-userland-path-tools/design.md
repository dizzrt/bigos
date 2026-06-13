## Context

Stage 29 的目标是在当前 cwd、`cd`、`pwd` baseline 之上，让路径和元数据 contract 通过交互式 shell 与小型静态用户程序更自然地被消费。当前系统已经具备单核 x86_64 runtime、`int 0x80` syscall、fd/VFS、RAM-backed `/rw`、只读 boot assets、cwd/相对路径、最小 metadata、用户 libc、`/bin/sh` 和若干 `/bin/*` 程序。

本设计影响正常内核初始化之后的 process/syscall/fs/userland 路径，不改变 bootloader、linker script、固定虚拟地址、页表布局、IDT/syscall vector、磁盘分区发现、exFAT boot image discovery 或 CR3 切换契约。所有新增用户态程序仍是 freestanding C 静态 ELF64，继续通过现有用户程序构建与打包路径进入镜像。

## Goals / Non-Goals

**Goals:**

- 提供一组小型、有界、用户可见的路径工具，使用户能从 shell 观察目录内容、文件内容、元数据、目录创建和路径删除结果。
- 复用现有 cwd、relative path、fd/VFS、metadata、errno、libc wrapper、shell 外部命令和用户程序打包 contract，不引入新的 broad kernel abstraction。
- 让每个工具具备确定性输出、错误报告和退出状态，便于行为导向验证与手工验证。
- 保持只读 boot assets 与 RAM-backed `/rw` 后端差异可观察，并避免把 `/rw` 描述为持久完整可写文件系统。

**Non-Goals:**

- 不实现完整 POSIX utility suite、完整 `ls`/`find`/`cp`/`mv`/`rm` 语义、递归遍历、通配符、排序稳定性承诺、脚本语言或完整 shell grammar。
- 不引入 rename、link、symlink、mount namespace、`chroot`、完整 path canonicalization、权限数据库、ACL、xattr 或 persistent writable filesystem。
- 不改变 syscall ABI 编号约定、用户寄存器调用约定、interrupt/IRQ 行为、page-table layout、boot image layout 或 storage driver contract。
- 不要求 SMP、async I/O、dynamic linking、hosted libc、threads、locale 或完整 stdio。

## Decisions

1. 工具以小型静态 C 程序为主，而不是在 shell 中内建完整工具集。
   - Rationale: 已有用户程序构建、`fork`/`execve`/`wait`、PATH 查找、fd 继承和 libc wrapper 能直接消费这些工具；这也能验证真实 kernel-to-userland contract。
   - Alternative considered: 把所有能力做成 shell builtin。该方案实现更快，但会弱化 `execve`、用户程序打包、libc wrapper 和 fd 继承路径的组合验证价值。

2. 第一批路径工具保持单一目的和有界输出。
   - Rationale: `ls` 关注目录项和基础类型/尺寸可见性，`cat` 关注文件内容读取，`stat` 关注元数据字段，`mkdir` 和 `rm` 关注 `/rw` 中创建与删除的路径效果。每个工具都可以用现有 syscall 和 libc wrapper 表达。
   - Alternative considered: 直接实现更大命令族或 POSIX 兼容选项。该方案会把阶段目标扩展成工具套件与 shell 语言问题，超出 roadmap 边界。

3. 路径解析以 kernel/VFS cwd contract 为权威，工具不做独立 namespace 或 canonicalization。
   - Rationale: 这样可以避免用户态和内核态路径语义分叉，并继续让现有 user-buffer validation、errno 和 VFS 后端差异成为单一事实来源。
   - Alternative considered: 工具在用户态拼接绝对路径。该方案容易绕过或重复 cwd/`.`/`..` 规则，并使后续 rename 和 FS 语义硬化更难验证。

4. 验证以行为可观察为主，并分层记录环境依赖。
   - Rationale: 路径工具的价值在于用户可见行为，验证应覆盖 shell 命令、stdout/stderr、退出状态、相对路径、只读/可写后端差异和失败路径。
   - Alternative considered: 只做源码字符串检查。该方案不能证明工具真正通过 kernel/libc/shell 路径工作，但可作为环境缺失时的替代检查。

## Risks / Trade-offs

- [Risk] 工具集合范围膨胀为完整 POSIX 工具套件 -> Mitigation: spec 只允许小型、有界、单一目的工具，并明确不支持 globbing、递归、复杂选项和完整 POSIX 语义。
- [Risk] 用户态工具绕过内核路径 contract 自行规范化路径 -> Mitigation: 工具必须把受支持路径交给 libc/kernel wrapper，错误以 kernel errno 为来源。
- [Risk] 目录列举和元数据输出过度承诺稳定排序或完整字段 -> Mitigation: 输出只要求确定性到可验证边界，不承诺 POSIX inode、完整权限、时间戳、ACL、xattr 或排序数据库。
- [Risk] 运行时验证受本地工具链、QEMU/Bochs、ROM/display 或镜像路径影响 -> Mitigation: tasks 中分离 source/build/runtime 检查；不可运行时记录跳过原因、替代检查和剩余风险。
- [Risk] `/rw` 删除工具被误解为持久文件系统能力 -> Mitigation: 文档和工具错误报告继续把 `/rw` 描述为 RAM-backed bounded runtime area，不承诺跨重启持久化。

## Migration Plan

1. 先补齐或整理用户态 libc/header 对目录枚举、元数据、mkdir/unlink/open/read 的最小 wrapper 暴露，确认它们不声明未实现 hosted/POSIX 接口。
2. 增加或硬化小型 `user/bin/*` 路径工具，并通过现有构建系统打包进 `/bin`。
3. 确认 `/bin/sh` 能通过 PATH 或显式路径运行这些工具，并让相对路径、重定向和错误输出保持有界可观察。
4. 增加行为导向验证或 smoke consumer，覆盖只读 boot assets、`/rw` 创建/删除、相对路径、metadata 输出和失败路径。
5. 若运行时验证发现某个后端行为未准备好，保持工具对该路径返回确定性错误，而不是扩大后端或 POSIX 语义。

## Open Questions

- 第一批实现是否只新增缺失工具，还是同时重构已有 `cat`、`stat`、`pwd` 的输出和错误报告格式，需要在实现前根据当前源码状态确认。
- 目录列举输出是否需要固定排序，还是仅要求单次枚举输出在同一后端状态下足够确定，需要结合现有 VFS directory enumeration 行为决定。
- 是否增加专门的 userland smoke 入口，还是复用现有 `userland_smoke` 扩展路径工具断言，需要在实现时结合验证成本决定。
