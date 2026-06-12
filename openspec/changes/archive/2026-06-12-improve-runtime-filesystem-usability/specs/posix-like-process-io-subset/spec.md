## ADDED Requirements

### Requirement: 运行时文件操作纳入有界 POSIX-like I/O 子集

BigOS SHALL 将运行时文件创建、打开、读取、写入、定位、同步、目录创建、最小目录枚举和删除纳入当前有界 POSIX-like 进程与 I/O 子集。该子集 MUST 继续明确不是完整 POSIX：不提供 session、terminal process group、job control、完整权限模型、完整目录遍历、完整 POSIX `readdir/getdents` 兼容、rename、link、symlink、文件锁、async I/O、SMP、动态链接、完整 POSIX libc 或 broad file-backed `mmap`。

#### Scenario: 文档描述有界文件 I/O 子集
- **WHEN** BigOS 文档、OpenSpec 或用户程序说明描述运行时文件 I/O
- **THEN** 它们 MUST 将行为描述为有界 POSIX-like 子集
- **AND** MUST NOT 暗示完整 POSIX 文件系统、权限、目录或终端语义

#### Scenario: 简单程序可依赖子集
- **WHEN** 简单静态 C 程序只使用已文档化文件 wrapper 和 errno 语义
- **THEN** 程序 MUST 能依赖该子集在 `/rw` 中进行运行期文件操作
- **AND** 程序 MUST NOT 需要 hosted runtime、动态加载器、完整 libc 或持久文件系统

#### Scenario: 简单程序可观察目录项变化
- **WHEN** 简单静态 C 程序使用最小目录枚举观察 `/rw` 目录
- **THEN** 程序 MUST 能看到文件创建、目录创建和 unlink 后的有界目录项结果
- **AND** 程序 MUST NOT 依赖 POSIX `DIR*`、完整 `struct dirent`、排序或跨调用快照语义

### Requirement: 文件 I/O 与进程/fd 组合语义稳定

BigOS SHALL 保证运行时文件 I/O 与 `fork`、`execve`、`wait`、fd 继承、dup/dup2、pipe 和 shell 重定向组合时保持有界且可观察。继承或复制的 fd MUST 指向文档化 open file object；exec MUST 保持或关闭 fd 的行为符合 close-on-exec 规则；失败的重定向或文件打开 MUST 不破坏父进程和 shell 的无关 fd。

#### Scenario: exec 后继承文件 fd
- **WHEN** 进程打开 `/rw` 文件后 exec 一个简单 C 程序且该 fd 未标记 close-on-exec
- **THEN** 新程序 MUST 能按继承的 fd 权限继续访问该文件
- **AND** 该行为 MUST 不要求完整 POSIX 进程模型或动态链接

#### Scenario: 重定向失败不破坏 shell
- **WHEN** shell 为命令设置输入或输出重定向时文件打开失败
- **THEN** shell MUST 报告确定性错误并继续运行
- **AND** shell 自身标准 fd 和无关 fd MUST 保持可用
