## ADDED Requirements

### Requirement: 最小 libc 暴露运行时文件 I/O wrapper

BigOS 用户态 libc SHALL 为简单静态 C 程序暴露运行时文件系统所需的最小 wrapper、类型、常量和 errno 翻译，包括 `open`、`read`、`write`、`close`、`lseek`、`fsync`、`mkdir`、`unlink`、最小目录枚举 wrapper 及其已支持 flags/whence/mode/目录项记录类型。wrapper MUST 复用现有 `int 0x80` ABI 与统一 errno 来源，失败时 MUST 设置用户态 `errno` 并返回接口约定的失败哨兵，成功时 MUST 返回用户态语义值。该能力 MUST NOT 引入 hosted `FILE` 文件流、`fopen`/`fclose`/`fflush`、locale、线程或完整 POSIX libc。

#### Scenario: C 程序通过 wrapper 操作文件
- **WHEN** 简单 C 程序包含 BigOS 用户态头并调用 `open`、`write`、`lseek`、`read`、`fsync` 和 `close`
- **THEN** 程序 MUST 能在 freestanding 用户态构建中解析声明和常量
- **AND** wrapper MUST 按 syscall ABI 发起调用并返回用户态语义值

#### Scenario: wrapper 失败设置 errno
- **WHEN** 文件 wrapper 收到内核负 errno，例如 `-ENOENT`、`-EROFS`、`-ENOSPC`、`-EACCES`、`-EBADF` 或 `-EINVAL`
- **THEN** wrapper MUST 设置对应正 errno 并返回 `-1` 或该接口文档化失败值
- **AND** 成功调用 MUST NOT 清零或改写既有 `errno`

#### Scenario: C 程序枚举目录项
- **WHEN** 简单 C 程序调用最小目录枚举 wrapper 读取 `/rw` 目录
- **THEN** libc MUST 提供有界目录项记录定义和 wrapper 声明
- **AND** wrapper MUST 返回已写入记录/字节数量或在失败时设置 errno

### Requirement: 文件相关头文件边界稳定

BigOS 用户态 libc SHALL 在细粒度头文件中声明运行时文件系统所需的最小接口和常量，至少覆盖 `unistd.h`、`fcntl.h`、`sys/types.h`、`sys/stat.h`、最小目录枚举头或等价 BigOS 映射。头文件 MUST 只声明已实现且有规格约束的接口，MUST NOT 通过宿主头、未实现函数或 umbrella 头扩大兼容承诺。

#### Scenario: 文件常量可用于简单程序
- **WHEN** 简单 C 程序使用 `O_RDONLY`、`O_WRONLY`、`O_RDWR`、`O_CREAT`、`O_TRUNC`、`SEEK_SET`、`SEEK_CUR`、`SEEK_END` 和 mode 类型
- **THEN** BigOS 用户态头文件 MUST 提供与内核 ABI 一致的定义
- **AND** 这些定义 MUST NOT 依赖宿主 OS 头文件

#### Scenario: 未实现接口不被声明为可用
- **WHEN** 审查用户态 libc 公共头
- **THEN** 头文件 MUST NOT 声明未实现的 `fopen`、POSIX `opendir`/`readdir` 兼容、`rename`、link、symlink、`mmap` 文件映射或完整 `stat` 数据库语义
- **AND** 任一新增声明 MUST 对应实现或单独规格变更
