## Why

当前默认用户态已经具备 `/bin/sh`、cwd、fd/VFS、pipe、ANSI 控制台、raw terminal mode、wall-clock 和阻塞式 sleep syscall，但日常交互仍缺少基础查看、复制、筛选、分页和文本处理工具。补齐一组有界小工具可以让 BigOS 在现有内核能力下更适合手工调试、文件系统验证和用户态 smoke 组合。

## What Changes

- 扩展 shell 内建命令：新增 `pwd`、`help`、`env`、`clear`、`true`、`false`；`pwd` 从外部 `/bin/pwd` 迁移为内建并移除默认打包的外部 `pwd`。
- 新增有界外部用户程序：`cp`、`mv`、`tee`、`write`、`append`、`head`、`tail`、`wc`、`grep`、`hexdump`、`date`、`kill`、`basename`、`dirname`、`more`、`find`、`du`、`sleep`。
- 这些工具只使用现有用户 libc 与 syscall 能力：路径、fd/VFS、目录遍历、signal、wall-clock、ANSI console、BigOS terminal raw/canonical mode、阻塞式 `sleep()`。
- `grep` 只支持普通子串匹配；`more` 只实现 BigOS 单终端有界分页；`du` 统计 metadata size 而不是块占用；`sleep` 使用现有 coarse tick-based 阻塞 sleep。
- `touch` 完整时间戳更新不纳入本次变更：当前文件 metadata 与用户 `struct stat` 未暴露 atime/mtime/ctime，也没有 `utime`/`utimens` syscall。可在后续独立文件时间戳契约中补齐。
- 非目标：不引入完整 POSIX 工具集、shell 变量展开/引用/glob/脚本语法、后台任务、完整 termios、正则表达式、符号链接、挂载命名空间、动态链接或 hosted libc。

## Capabilities

### New Capabilities

- `userland-daily-tools`: 定义 BigOS 日常交互工具集合的有界行为、打包和验证边界。

### Modified Capabilities

- `user-shell`: shell 内建命令集合从最小 baseline 扩展到日常交互内建，并将 `pwd` 从外部工具迁移为内建。
- `userland-path-tools`: 外部路径/文本工具集合扩展为可组合的日常工具子集。

## Impact

- 影响 `user/sh/sh.c`：新增内建命令、help 输出和环境打印。
- 影响 `user/bin/**`：新增多个 freestanding C 用户程序；移除默认外部 `pwd`。
- 影响 `xmake/user_package.lua` 与 `tools/bigosdev/core.py`：更新默认 `/bin` 程序构建与镜像打包清单。
- 影响验证：需要至少运行用户程序构建；环境允许时可运行默认 QEMU headless 或 userland 相关 smoke。不会改变 boot 地址、链接地址、磁盘布局、IDT/syscall vector、CR3 切换或现有 syscall ABI。
