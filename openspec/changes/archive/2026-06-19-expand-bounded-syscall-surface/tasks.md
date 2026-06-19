## 1. ABI 与现状审查

- [x] 1.1 审查现有 syscall 编号、`int 0x80` 参数寄存器、负 errno 约定、用户指针 copy helper 和 source-level ABI 检查，确认新增编号只能追加且不重排既有 ABI。
- [x] 1.2 审查现有 `wait`、parent-child、zombie/reap、safe teardown 和信号退出状态路径，确认 wait 变体可复用的状态编码、阻塞点和错误边界。
- [x] 1.3 审查 fd table、`fork`、`dup`、`dup2`、`execve`、close 和 reap 路径，确认 close-on-exec flag 的存储位置、继承规则和 exactly-once close 风险。
- [x] 1.4 审查 VFS path resolution、cwd、metadata、access-like 权限判断、truncate、read-only backend 和 `/rw` writable backend status 映射，确认新增文件/路径 primitive 的共享入口。

## 2. Wait 变体与进程 primitive

- [x] 2.1 扩展内核 wait helper，使其支持 `WAIT_ANY`、指定 child pid 和 `WNOHANG` 的有界等待，并对 no-child、非法 selector、无效 status 指针和 unsupported options 返回确定性 errno。
- [x] 2.2 将 bounded wait 变体接入 syscall dispatcher，保持现有 `SYS_WAIT` 兼容性或以追加 syscall 方式暴露新语义，不改变既有 syscall 编号。
- [x] 2.3 补齐进程信息 primitive 的内核查询边界，覆盖 pid、ppid、pgid、sid、uid、gid 和不存在目标 pid 的确定性失败。
- [x] 2.4 增加或更新 source-level tests，覆盖 wait 任意子进程、wait 指定子进程、`WNOHANG` 命中/未命中、unsupported options、no-child、status copy 失败和进程信息查询。

## 3. fd close-on-exec 与控制面

- [x] 3.1 为 fd table entry 增加 close-on-exec state，确保初始化、close、reap、fd table grow/shrink 和错误回滚路径不会读取未初始化 flag。
- [x] 3.2 定义并实现 `fork`、`dup`、`dup2` 与 close-on-exec flag 的有界继承/清除规则，覆盖目标 fd 覆盖关闭和失败不变性。
- [x] 3.3 在 `execve` commit 阶段关闭所有 close-on-exec fd，保证 marked descriptors exactly-once close，exec rollback 不关闭或清除旧 fd flag。
- [x] 3.4 增加有界 fd-control syscall 和 libc wrapper，支持 `F_GETFD`、`F_SETFD`、`FD_CLOEXEC`、`F_DUPFD` 子集，并对 unsupported command、bad fd、非法最小 fd 和 fd table 容量不足返回确定性 errno。
- [x] 3.5 增加 source-level tests 或用户态验证，覆盖 close-on-exec set/get、`F_DUPFD` 最低可用 fd 分配、`F_DUPFD` 新 fd 清除 close-on-exec、fork 继承、dup/dup2 规则、exec commit close、exec rollback 保留和 bad-fd 失败。

## 4. 文件/路径 primitive

- [x] 4.1 通过共享 VFS path helper 实现 bounded `access` 类检查，覆盖绝对路径、cwd-relative 路径、unsupported mode bits、deleted cwd、read-only backend 和 `/rw` 权限失败。
- [x] 4.2 对齐 `stat`/`fstat` 用户态结构与内核 metadata contract，确认用户 buffer copy、目录/普通文件字段、缺失路径、bad fd 和 backend status 映射。
- [x] 4.3 统一 path `truncate` 与 `ftruncate` wrapper 语义，覆盖 writable `/rw` regular file、read-only path、directory、pipe/unsupported fd、非法长度、容量失败和失败不推进状态。
- [x] 4.4 增加 source-level tests，覆盖新增 file/path primitive 的成功路径、错误码、用户指针失败、相对路径解析和不可阻塞上下文 guard。

## 5. libc、用户程序与 shell 消费

- [x] 5.1 更新 freestanding libc headers 与 syscall wrapper，提供 bounded `wait`/`waitpid`、`WNOHANG`、status helper、fd-control、`F_DUPFD`、access、metadata、truncate 和进程信息声明。
- [x] 5.2 确认 libc wrapper 按现有约定将负 kernel errno 转换为 user-visible `errno`，并对 unsupported flags/options 设置确定性 errno。
- [x] 5.3 更新小型用户验证程序或新增 bounded helper，用于观察 wait 变体、close-on-exec、metadata/access/truncate 和进程信息 primitive。
- [x] 5.4 审查 `/bin/sh` 与 packaged `/bin/*`，在有收益的位置改用标准形态 wrapper，同时保持既有 foreground command、single pipe、redirection 和 unsupported job-control 边界。

## 6. 文档与 OpenSpec 边界同步

- [x] 6.1 更新 syscall/userland 相关英文文档，说明 expanded bounded syscall surface、wait 变体、fd close-on-exec、file/path primitive、进程信息 primitive 和 non-goals。
- [x] 6.2 同步更新对应 `docs/zh` 镜像文档，保持相同相对路径、能力边界和不支持项描述。
- [x] 6.3 更新 header 注释或 source-adjacent notes，明确完整 POSIX wait/fcntl/job control/nonblocking I/O/dynamic linking/SMP 仍不支持。
- [x] 6.4 审查 `roadmap.md` 和 OpenSpec artifact 语言，确保不新增源码级入口、命令、验证 marker、archive 索引或路线图任务编号引用。

## 7. 构建、静态检查与运行验证

- [x] 7.1 运行默认 `xmake`，使用 x86_64-elf cross toolchain 验证 kernel 与默认 user init/shell 构建；若 toolchain 或 xmake 不可用，在 validation notes 中记录 blocker、替代检查和剩余风险。
- [x] 7.2 运行与 GCC cross-build 环境尽量接近的 clang C++ 辅助检查，覆盖修改过的 `kernel/core/syscall`、`kernel/core/proc`、`kernel/core/fs` 和相关 headers；修复当前 change 引入的有效诊断。
- [x] 7.3 运行 clangd 辅助诊断或记录不可用原因，区分历史诊断、当前 change 诊断和 freestanding 配置 false positive。
- [x] 7.4 运行相关 source-level pytest，使用 `uv run pytest ...` 覆盖 syscall entry、process lifecycle、fd/VFS、user libc、shell 或新增 source contract；若 `uv` 或 pytest 不可用，记录 blocker。
- [x] 7.5 在可用环境中运行 QEMU headless 用户态行为验证，观察 wait 变体、close-on-exec、metadata/access/truncate、进程信息 primitive 和 shell 回归行为。
- [x] 7.6 如 QEMU、Bochs、ROM/display 依赖、raw disk image、serial oracle 或 timeout 配置不可用，记录未运行项、原因、替代 build/static checks 和剩余 runtime 风险。
- [x] 7.7 形成 validation notes，分别列出通过的检查、无法运行的检查、历史诊断、当前 change 引入并已修复的问题，以及仍需后续处理的风险。
