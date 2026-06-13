## ADDED Requirements

### Requirement: 最小 libc 暴露 rename wrapper

BigOS 用户态 libc SHALL 为简单静态 C 程序暴露受限 `rename(const char *oldpath, const char *newpath)` wrapper 及其头文件声明。wrapper MUST 复用现有 `int 0x80` ABI 与统一 errno 来源，成功时返回 `0`，失败时把内核负 errno 翻译为用户态正 `errno` 并返回 `-1`。该能力 MUST NOT 引入 `renameat`、`renameat2`、hosted `FILE` 文件流、完整 POSIX libc、线程、动态链接或宿主 OS 依赖。

#### Scenario: C 程序通过 wrapper rename 文件

- **WHEN** 简单 C 程序包含 BigOS 用户态文件相关头并调用 `rename("/rw/a", "/rw/b")`
- **THEN** 程序 MUST 能在 freestanding 用户态构建中解析声明
- **AND** wrapper MUST 按 syscall ABI 发起调用并在成功时返回 `0`

#### Scenario: wrapper 失败设置 errno

- **WHEN** rename wrapper 收到内核负 errno，例如 `-ENOENT`、`-EEXIST`、`-EROFS`、`-EXDEV`、`-EACCES`、`-ENOSPC`、`-EFAULT` 或 `-EINVAL`
- **THEN** wrapper MUST 设置对应正 errno 并返回 `-1`
- **AND** 成功调用 MUST NOT 清零或改写既有 `errno`

### Requirement: rename 头文件边界稳定

BigOS 用户态 libc SHALL 只在 freestanding-safe 的文件相关头文件中声明已实现且有规格约束的 rename wrapper 和必要 errno 常量。头文件 MUST 继续避免宿主系统头依赖，MUST NOT 通过 umbrella 头或未实现函数扩大兼容承诺。

#### Scenario: 文件头声明受限 rename

- **WHEN** 用户程序包含 BigOS 用户态 `unistd.h`、`stdio.h`、`fcntl.h` 或项目选择的文件操作头
- **THEN** 头文件 MUST 暴露受限 `rename` 所需的原型和 errno 常量
- **AND** 声明 MUST 不要求宿主 libc、动态链接器、线程 runtime 或完整 POSIX namespace

#### Scenario: 未实现 rename 扩展不被声明

- **WHEN** 审查用户态 libc 公共头
- **THEN** 头文件 MUST NOT 声明未实现的 `renameat`、`renameat2`、link、symlink、文件交换或完整目录 rename 接口
- **AND** 任一新增声明 MUST 对应 BigOS 用户态 libc 中的实现或明确的后续规格变更
