## Context

默认用户态已经具备运行这些工具所需的大部分基础：`/bin/sh` 支持内建、PATH、外部命令、单级 pipe 与基本重定向；用户 libc 暴露 `open/read/write/close/lseek/stat/fstat/opendir/readdir/rename/unlink/kill/time/sleep` 等包装；控制台输出侧已有有界 ANSI/CSI 解析；默认终端已有 BigOS-specific raw/canonical mode；阻塞式 sleep 已经通过 `SYS_SLEEP_MS` 和 libc `sleep()` 暴露。

本变更只扩展 `user/**` 的日常工具与打包清单，不改变 boot loader、磁盘布局、链接地址、IDT/syscall vector、页表布局或现有 syscall 编号。架构假设仍是 x86_64 freestanding C17 用户程序，经 `x86_64-elf-gcc`、user crt0 与最小 libc 静态链接为有界 ELF。

## Goals / Non-Goals

**Goals:**

- 在 shell 进程内实现必须影响 shell 状态或交互体验的内建：`pwd`、`help`、`env`、`clear`、`true`、`false`。
- 移除默认 `/bin/pwd` 外部程序，避免同名外部工具与内建语义重复。
- 新增一组小型静态外部程序，覆盖复制、移动、写入、追加、查看、筛选、十六进制查看、日期、信号、路径名处理、分页、遍历、大小估算和阻塞 sleep。
- 保持所有工具能力有界，错误通过 errno 或确定性文本报告，返回非零状态而不是扩大 kernel/libc 语义。
- 更新构建与镜像打包清单，使默认启动镜像可通过 `/bin` 与 shell PATH 访问这些工具。

**Non-Goals:**

- 不实现完整 POSIX shell、变量展开、引用、glob、脚本控制流、后台任务或 job table。
- 不实现完整 POSIX 工具选项集、正则表达式、locale、符号链接、跨设备移动回退、递归删除、权限修改或挂载语义。
- 不补文件 atime/mtime/ctime。`touch` 完整时间戳能力需要先扩展 metadata、VFS/bigfs 持久化格式和新的 syscall/libc 契约，不能作为本次用户工具补齐的顺带项。

## Decisions

1. shell 内建只放入 shell 状态相关或零成本交互命令。

   `pwd` 读取当前 shell cwd，`help` 描述当前有限能力，`env` 打印 shell 传递给子进程的环境，`clear` 输出 ANSI 清屏序列，`true/false` 只设置状态码。文件遍历、复制、筛选、分页等逻辑保持为外部程序，以继续覆盖 `fork/execve/wait`、PATH、fd、pipe 和重定向路径。

2. 外部程序保持单文件、小缓冲、顺序 I/O。

   每个工具使用固定大小缓冲和现有 libc API，不引入共享库或复杂抽象。`grep` 只做普通子串匹配；`tail` 对 regular file 使用 `stat/lseek/read`，对 stdin 可退化为固定窗口；`du` 累加 metadata size；`find` 有界递归深度和路径长度；`more` 使用 BigOS terminal mode 进入 raw mode 读取分页按键，退出时恢复 canonical。

3. `write` 与 `append` 作为显式文件写入工具。

   `write PATH TEXT...` 覆盖写入文件；`append PATH TEXT...` 通过 `lseek(fd, 0, SEEK_END)` 追加。当前 libc 没有 `O_APPEND`，所以追加只声明为单进程有界工具语义，不声明并发原子追加。

4. 时间戳 touch 不合并进本次实现。

   当前 `bigos::Metadata` 与用户 `struct stat` 只有 type/mode/uid/gid/nlink/size/object_id，reserved 字段不能在没有规范和持久化策略的情况下临时复用。完整 `touch` 需要独立变更定义 timestamp 表示、更新时机、bigfs/exFAT 行为、syscall 和兼容性。

## Risks / Trade-offs

- [Risk] 默认 `/bin` 程序数量增加，镜像打包和单个 ELF 64 KiB 上限更容易触发。→ 每个工具保持小缓冲、小实现；构建阶段继续执行现有 size limit。
- [Risk] `more` raw mode 退出路径异常可能留下 raw mode。→ 工具在正常 EOF、用户退出和错误返回前都调用 canonical restore；shell 仍保留现有前台恢复路径。
- [Risk] `find`/`du` 递归可能消耗栈或超出路径上限。→ 使用固定最大深度和 `SH_PATH_MAX`/syscall path 上限内的路径拼接，超限确定性报错。
- [Risk] 工具名接近 POSIX 但能力更小。→ help 文本、spec 和源码注释保持 bounded BigOS 描述。
