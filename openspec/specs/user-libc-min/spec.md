## Purpose

定义 BigOS 最小用户态 libc 能力：为已暴露 syscall 提供 ABI wrapper 与 errno 翻译，提供 freestanding 字符串/内存函数、最小堆分配、标准输入输出、只读环境变量访问和细粒度标准头文件边界，支撑早期用户程序、shell 和简单静态 C 程序运行。
## Requirements
### Requirement: 系统调用 wrapper 与 errno 翻译

BigOS 用户态 libc SHALL 为内核 `int 0x80` ABI（见 `bigos::sys::SyscallNumber`）的每个已暴露 syscall 提供 C 函数 wrapper，按固定寄存器约定（number -> rax，参数 -> rdi/rsi/rdx/r10/r8/r9，返回值 <- rax）发起 syscall。wrapper MUST 把内核返回的负 errno 值翻译为 POSIX 风格约定：把 `errno` 置为对应正值并返回 `-1`（或对应失败哨兵值），成功时直接返回内核结果。errno 码 MUST 复用单一来源 `include/bigos/errno.h`，不得在用户态另立一套数值。

#### Scenario: wrapper 按 ABI 发起 syscall

- **WHEN** 用户程序调用一个 libc syscall wrapper（如 `write(fd, buf, len)`）
- **THEN** wrapper MUST 把 syscall number 放入 rax、参数按固定顺序放入 rdi/rsi/rdx/r10/r8/r9 并执行 `int 0x80`
- **AND** wrapper MUST 从 rax 读取返回值

#### Scenario: 负 errno 翻译为 errno + -1

- **WHEN** 内核 syscall 返回一个负 errno 值（如 `-EBADF`）
- **THEN** wrapper MUST 把全局 `errno` 置为对应正值（如 `EBADF`）
- **AND** wrapper MUST 返回 `-1`（或该 wrapper 约定的失败哨兵值）

#### Scenario: 成功结果原样返回

- **WHEN** 内核 syscall 返回一个非负结果
- **THEN** wrapper MUST 原样返回该结果且 MUST NOT 改写 `errno`

### Requirement: libc 暴露 cwd 与相对路径 wrapper

BigOS 用户态 libc SHALL expose bounded current-directory wrappers and path-taking wrappers for simple static C programs. At minimum, libc MUST provide declarations and implementations for `chdir` and `getcwd`, expose `ERANGE` for cwd buffer-size failures, and let existing path-taking wrappers pass relative paths to the kernel unchanged so the kernel/VFS cwd contract remains authoritative. Wrappers MUST follow the existing syscall ABI, translate kernel negative errno returns into positive user `errno` plus documented failure sentinels, and MUST NOT claim complete POSIX libc support.

#### Scenario: chdir wrapper 成功

- **WHEN** 用户程序调用 `chdir` 指向一个存在目录且内核返回成功
- **THEN** libc MUST return the documented success value
- **AND** subsequent relative path wrappers in the same process MUST observe the new cwd through kernel path resolution

#### Scenario: chdir wrapper 失败设置 errno

- **WHEN** 内核因缺失路径、非目录、无效用户路径或不支持路径形式拒绝 `chdir`
- **THEN** libc MUST set `errno` to the corresponding positive value
- **AND** MUST return the documented failure sentinel without changing userland shadow state

#### Scenario: getcwd wrapper 返回当前路径

- **WHEN** 用户程序以足够大的缓冲区调用 `getcwd`
- **THEN** libc MUST invoke the kernel cwd query through the documented syscall ABI
- **AND** MUST return a user-visible pointer or success result consistent with the wrapper contract

#### Scenario: getcwd wrapper 小缓冲设置 ERANGE

- **WHEN** 内核因用户缓冲区容量不足返回 `-ERANGE`
- **THEN** libc MUST set `errno` to `ERANGE`
- **AND** MUST return the documented failure sentinel without modifying caller-visible cwd state

#### Scenario: path wrapper 不自行 canonicalize

- **WHEN** 用户程序通过 libc 调用 `open`、`stat`、`execve` 或其他 path-taking wrapper with a relative path containing ordinary components, `.`, or `..`
- **THEN** libc MUST pass the bounded user path to the kernel without implementing its own namespace, symlink, `chroot`, or full `realpath` behavior
- **AND** kernel negative errno MUST remain the single source for failure translation

### Requirement: cwd 头文件边界

BigOS 用户态 libc SHALL expose cwd-related declarations only in freestanding-safe headers that already represent the bounded userland ABI. Headers MUST include only implemented constants, types, and prototypes needed for cwd and path wrappers, and MUST NOT imply hosted filesystem APIs, thread-safe cwd databases, dynamic loader behavior, or complete POSIX path support.

#### Scenario: 头文件可供简单 C 程序包含

- **WHEN** 简单静态 C 程序包含 BigOS userland headers for path operations
- **THEN** the program MUST see declarations for supported cwd wrappers and path-taking wrappers
- **AND** those declarations MUST NOT depend on host libc, OS services, threads, shared libraries, or dynamic initialization

#### Scenario: 未实现接口不被声明为支持

- **WHEN** 文档或 headers 描述 cwd/path support
- **THEN** they MUST describe it as a bounded BigOS subset with POSIX-style `.`/`..` component handling
- **AND** MUST NOT claim full POSIX `realpath`, `fchdir`, `openat`, symlink, mount namespace, or thread-local cwd semantics unless a later spec adds them

### Requirement: 最小字符串与内存函数

BigOS 用户态 libc SHALL 提供一组最小且 freestanding 的字符串与内存函数（至少 `strlen`、`strcmp`、`strncmp`、`strcpy`、`strncpy`、`memcpy`、`memset`、`memmove`），其行为 MUST 符合标准 C 语义且在有界输入下确定，不依赖任何宿主运行时。

#### Scenario: 字符串/内存函数符合标准语义

- **WHEN** 用户程序调用 libc 字符串或内存函数
- **THEN** 函数 MUST 返回符合标准 C 语义的结果
- **AND** `memmove` MUST 正确处理源与目标区间重叠的情形

### Requirement: 最小用户态堆分配

BigOS 用户态 libc SHALL 提供基于 `SYS_BRK`（可选辅以 `SYS_MAP_ANON`）的最小 `malloc`/`free`。分配器 MUST 是有界且确定性的：无法满足请求时 `malloc` MUST 返回 NULL，且 MUST NOT 破坏既有已分配块或进入未定义状态。

#### Scenario: malloc 返回可用内存

- **WHEN** 用户程序调用 `malloc(n)` 且堆可经 `brk` 扩展满足请求
- **THEN** `malloc` MUST 返回一个对齐、可写、容量至少为 `n` 字节的指针

#### Scenario: 分配失败返回 NULL

- **WHEN** `malloc(n)` 无法经 `brk`/`mmap` 满足请求
- **THEN** `malloc` MUST 返回 NULL
- **AND** 既有已分配块 MUST 保持有效

### Requirement: 最小标准输入输出

BigOS 用户态 libc SHALL 提供基于 fd 0/1/2 的最小 stdio：对 `read`/`write` 的薄封装，以及 `putchar`、`puts` 与一个最小 `printf`（至少支持 `%s`、`%d`、`%x`、`%c`、`%%`）。最小 stdio MUST NOT 假定完整 `FILE` 缓冲流语义。

#### Scenario: printf 输出格式化文本到 stdout

- **WHEN** 用户程序调用 `printf` 并带受支持的格式说明符
- **THEN** libc MUST 把格式化结果经 fd 1（`write`）输出
- **AND** 受支持的格式说明符 MUST 产生符合标准 C 语义的文本

### Requirement: 最小环境变量只读访问

BigOS 用户态 libc SHALL 提供最小的环境变量只读访问：crt0 传入的 `envp` MUST 可经 `environ` 指针与 `getenv(name)` 读取。`getenv` MUST 在变量存在时返回其值字符串、不存在时返回 NULL。本能力仅提供只读访问，MUST NOT 要求实现 `setenv`/`putenv`/`unsetenv` 等环境数据库写入语义。

#### Scenario: getenv 读取已存在变量

- **WHEN** 用户程序调用 `getenv("PATH")` 且 `envp` 中存在 `PATH=...`
- **THEN** `getenv` MUST 返回该变量的值字符串（`=` 之后的部分）

#### Scenario: getenv 读取不存在变量

- **WHEN** 用户程序调用 `getenv(name)` 且 `envp` 中无该变量
- **THEN** `getenv` MUST 返回 NULL

### Requirement: C 程序 syscall wrapper 约定稳定

BigOS 最小用户 libc SHALL 为简单 C 程序基线的简单 C 程序提供稳定 syscall wrapper 约定：成功时返回调用语义规定的值，失败时把内核负 errno 翻译为用户态 `errno` 并返回 `-1` 或该 wrapper 明确记录的失败哨兵。普通 C 程序 MUST NOT 需要直接解释内核负 errno。

#### Scenario: wrapper 成功返回用户态值

- **WHEN** 基线 C 程序调用一个成功的 libc syscall wrapper
- **THEN** wrapper MUST 返回该调用的用户态成功值
- **AND** 程序 MUST NOT 需要读取或转换内核内部负 errno 编码

#### Scenario: wrapper 失败设置 errno

- **WHEN** 基线 C 程序调用一个失败的 libc syscall wrapper
- **THEN** wrapper MUST 设置 `errno` 为对应错误码
- **AND** wrapper MUST 返回 `-1` 或该接口文档化的失败哨兵

### Requirement: 基础输出和错误报告可用于小型程序

BigOS 最小用户 libc SHALL 为 基线 C 程序提供基础 stdout/stderr 输出能力，使程序能打印普通结果和确定性错误说明。该能力 MUST 基于现有 fd/write 语义，且 MUST NOT 声称提供完整 hosted `stdio` 或完整 POSIX libc。

#### Scenario: 程序向 stdout 输出结果

- **WHEN** 基线 C 程序通过 libc 输出 helper 或 `write` wrapper 写入 stdout
- **THEN** 输出 MUST 经当前 fd/VFS/console 路径可被 shell、串口日志或控制台观察

#### Scenario: 程序向 stderr 输出错误

- **WHEN** 基线 C 程序遇到可报告错误
- **THEN** 程序 MUST 能通过 libc 或 `write` wrapper 向 stderr 输出确定性错误说明
- **AND** 该行为 MUST NOT 依赖完整 `FILE` 缓冲、locale 或 hosted runtime

### Requirement: 最小 C 标准库子集边界

BigOS 用户态 libc SHALL 将当前 freestanding 用户态支持定义为最小 C 标准库子集。该子集 MUST 覆盖简单静态 C 程序实际可用的 syscall wrapper、errno、字符串/内存函数、最小 stdio、standard streams、只读环境变量、基础堆分配、基础类型/常量、细粒度标准头文件名和程序入口协作；该子集 MUST NOT 声称提供 locale、线程、完整 hosted `stdio`、动态加载器、共享库、完整 POSIX libc 或广泛标准兼容。

#### Scenario: 简单 C 程序只依赖最小子集

- **WHEN** 一个 BigOS 简单静态 C 程序包含用户态 libc 公共头并调用已声明接口
- **THEN** 程序 MUST 能在 freestanding 用户态构建中解析这些声明
- **AND** 程序 MUST NOT 需要宿主 libc、动态链接器、共享库、线程 runtime 或完整 POSIX runtime

#### Scenario: 未实现标准能力不被声明为支持

- **WHEN** 文档、公共头或用户程序说明描述 BigOS 用户态 libc
- **THEN** 描述 MUST 明确这是 bounded minimal subset
- **AND** MUST NOT 暗示 locale、线程、完整 `FILE` 流、动态加载或完整 POSIX libc 已被支持

### Requirement: 标准头文件名暴露稳定最小接口

BigOS 用户态 libc SHALL 提供 freestanding-safe 的细粒度标准头文件名，声明简单 C 程序可依赖的最小类型、常量、syscall wrapper、字符串/内存函数、堆分配、stdio helper、standard streams 和环境访问接口。至少 MUST 提供 `stdio.h`、`stdlib.h`、`string.h`、`errno.h`、`unistd.h`、`fcntl.h`、`sys/types.h`、`sys/wait.h` 或等价的 BigOS 用户态头文件映射；每个头文件 MUST 只暴露 BigOS 当前实现且有规格约束的接口。BigOS MAY 保留一个 umbrella 头作为兼容入口，但 MUST NOT 以宿主系统头或未实现接口扩大兼容承诺。

#### Scenario: 细粒度头文件可在 freestanding 用户程序中使用

- **WHEN** 一个用户程序在 BigOS 用户态构建中包含 `stdio.h`、`stdlib.h`、`string.h`、`errno.h`、`unistd.h`、`fcntl.h`、`sys/types.h` 或 `sys/wait.h`
- **THEN** 这些头文件 MUST 提供其对应最小子集所需的 `size_t`、`ssize_t`、`off_t`、`pid_t`、`NULL`、open flag、seek whence、wait 常量和已支持函数原型
- **AND** 头文件 MUST NOT 依赖宿主 OS 服务、宿主 libc 初始化、动态链接或 C++ runtime

#### Scenario: umbrella 头不扩大兼容承诺

- **WHEN** 用户程序继续包含 BigOS umbrella libc 头
- **THEN** umbrella 头 MAY 重新导出细粒度头文件中的最小接口
- **AND** MUST NOT 声明超出细粒度头文件和规格约束的额外 hosted/POSIX 行为

#### Scenario: 头文件不声明未实现 hosted 行为

- **WHEN** 审查用户态 libc 头文件集合
- **THEN** 头文件 MUST NOT 声明完整 `FILE` 流、locale、线程、动态加载或未实现 POSIX 数据库接口
- **AND** 任一新增声明 MUST 对应 BigOS 用户态 libc 中的实现或明确的后续规格变更

### Requirement: errno mirror 与内核错误码保持一致

BigOS 用户态 libc SHALL 维护与内核单一错误码来源一致的用户态 errno mirror。用户态正 errno 常量数值 MUST 与内核对应错误码相同，syscall wrapper MUST 只在内核返回负 errno 时设置用户态 `errno`，成功返回时 MUST NOT 清除或改写既有 `errno`。

#### Scenario: 用户态 errno 数值与内核一致

- **WHEN** 用户态头文件定义 `EBADF`、`EINVAL`、`EFAULT`、`ENOSYS`、`ENOENT`、`ENOMEM` 等 BigOS 已支持 errno
- **THEN** 每个正 errno 数值 MUST 与内核单一错误码来源中的对应值一致
- **AND** 用户态 wrapper MUST 把内核返回的负值转换为相同正值写入 `errno`

#### Scenario: 成功调用不改写 errno

- **WHEN** 用户程序调用一个成功的 libc syscall wrapper
- **THEN** wrapper MUST 返回该接口的成功值
- **AND** wrapper MUST NOT 因成功调用清零或改写 `errno`

### Requirement: 有界堆分配失败语义稳定

BigOS 用户态 libc SHALL 提供有界且确定性的最小堆分配。`malloc` MUST 返回按目标 ABI 对齐且至少满足请求大小的可写内存，无法满足请求时 MUST 返回 NULL；`free(NULL)` MUST 无副作用；释放后的块 MAY 被后续 `malloc` 复用，但 libc MUST NOT 承诺线程安全、完整 coalescing 或 hosted allocator 行为。

#### Scenario: 分配返回对齐可写内存

- **WHEN** 用户程序调用 `malloc(n)` 且当前用户堆可以满足该请求
- **THEN** `malloc` MUST 返回非 NULL 指针
- **AND** 返回区域 MUST 至少可写 `n` 字节并满足 BigOS 用户态 ABI 所需对齐

#### Scenario: 分配失败不破坏既有块

- **WHEN** 用户程序已经持有有效分配块且后续 `malloc` 因有界堆或映射限制失败
- **THEN** 失败调用 MUST 返回 NULL
- **AND** 既有已分配块 MUST 保持可用且内容不因失败调用被破坏

#### Scenario: free NULL 无副作用

- **WHEN** 用户程序调用 `free(NULL)`
- **THEN** libc MUST 保持堆状态不变
- **AND** 调用 MUST NOT 触发 syscall、fault 或未定义控制流

### Requirement: 最小 stdio 行为可观察且有限

BigOS 用户态 libc SHALL 提供基于 fd 0/1/2 和 `read`/`write` wrapper 的最小 stdio helper。`stdin`、`stdout`、`stderr` MAY 使用最小 opaque `FILE` 表示，但该表示 MUST 仅承载 standard streams。`putchar`、`puts`、最小 `printf` 和 `fprintf(stderr, ...)` MUST 通过当前 fd/VFS/console 路径产生可观察输出；`printf`/`fprintf` MUST 至少支持 `%s`、`%d`、`%x`、`%c`、`%%`，并 MUST NOT 要求完整 `FILE` 缓冲、`fopen`/`fclose`、locale、浮点格式化或宽字符支持。

#### Scenario: 最小 printf 输出支持格式

- **WHEN** 用户程序调用 `printf` 并使用 `%s`、`%d`、`%x`、`%c` 或 `%%`
- **THEN** libc MUST 将格式化文本写入 stdout
- **AND** 输出 MUST 能经当前控制台、shell 输出或串口日志路径观察

#### Scenario: fprintf stderr 输出错误

- **WHEN** 用户程序调用 `fprintf(stderr, ...)` 并使用 `%s`、`%d`、`%x`、`%c` 或 `%%`
- **THEN** libc MUST 将格式化文本写入 stderr 对应的 fd 2
- **AND** 输出 MUST 能经当前错误输出、控制台或串口日志路径观察

#### Scenario: stdio 不依赖 FILE 流

- **WHEN** 用户程序使用 `putchar`、`puts`、最小 `printf` 或 `fprintf(stderr, ...)`
- **THEN** 这些 helper MUST 通过 fd/write 语义完成输出
- **AND** MUST NOT 依赖完整 `FILE` 对象、文件打开/关闭、缓冲刷新、locale、宽字符或宿主 stdio runtime

### Requirement: 最小 libc 行为验证

BigOS SHALL 为最小 C 标准库子集提供可分层验证路径。验证 MUST 覆盖源码/构建层面的接口一致性，以及专门 libc subset smoke 中运行时可观察的参数/环境、errno、stdout/stderr 输出、`fprintf(stderr, ...)`、字符串/内存函数、堆分配和 wrapper 成功/失败行为；依赖 QEMU、Bochs、交叉工具链或本地镜像环境的检查 MUST 保持分层，环境不可用时 MUST 记录跳过原因与残余风险。

#### Scenario: 行为检查覆盖 libc 子集

- **WHEN** 运行最小 libc subset smoke
- **THEN** 验证 MUST 覆盖至少一个简单 C 程序的正常输出、`fprintf(stderr, ...)` 错误报告、参数/环境读取、errno 翻译和堆分配行为
- **AND** 这些检查 MUST 以用户程序可观察输出、退出状态或确定性日志结果作为判断依据

#### Scenario: 环境依赖检查可被分层跳过

- **WHEN** 本地缺少 emulator、交叉工具链、显示/ROM 依赖或磁盘镜像配置
- **THEN** 对应运行时验证 MAY 被跳过
- **AND** 跳过记录 MUST 明确缺失条件、已执行的替代检查和剩余风险

### Requirement: 最小 libc 暴露运行时文件 I/O wrapper

BigOS 用户态 libc SHALL 为简单静态 C 程序暴露运行时文件系统所需的最小 wrapper、类型、常量和 errno 翻译，包括 `open`、`read`、`write`、`close`、`lseek`、`fsync`、`mkdir`、`unlink`、`rename`、最小目录枚举 wrapper 及其已支持 flags/whence/mode/目录项记录类型。wrapper MUST 复用现有 `int 0x80` ABI 与统一 errno 来源，失败时 MUST 设置用户态 `errno` 并返回接口约定的失败哨兵，成功时 MUST 返回用户态语义值。该能力 MUST NOT 引入 hosted `FILE` 文件流、`fopen`/`fclose`/`fflush`、locale、线程或完整 POSIX libc。

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

### Requirement: 文件相关头文件边界稳定

BigOS 用户态 libc SHALL 在细粒度头文件中声明运行时文件系统所需的最小接口和常量，至少覆盖 `unistd.h`、`fcntl.h`、`sys/types.h`、`sys/stat.h`、最小目录枚举头或等价 BigOS 映射。头文件 MUST 只声明已实现且有规格约束的接口，MUST NOT 通过宿主头、未实现函数或 umbrella 头扩大兼容承诺。

#### Scenario: 文件常量可用于简单程序
- **WHEN** 简单 C 程序使用 `O_RDONLY`、`O_WRONLY`、`O_RDWR`、`O_CREAT`、`O_TRUNC`、`SEEK_SET`、`SEEK_CUR`、`SEEK_END` 和 mode 类型
- **THEN** BigOS 用户态头文件 MUST 提供与内核 ABI 一致的定义
- **AND** 这些定义 MUST NOT 依赖宿主 OS 头文件

#### Scenario: 未实现接口不被声明为可用
- **WHEN** 审查用户态 libc 公共头
- **THEN** 头文件 MUST NOT 声明未实现的 `fopen`、POSIX `opendir`/`readdir` 兼容、link、symlink、`mmap` 文件映射或完整 `stat` 数据库语义
- **AND** 任一新增声明 MUST 对应实现或单独规格变更

### Requirement: 用户态 syscall wrapper 只消费稳定 ABI
BigOS userland libc SHALL implement syscall wrappers only through the documented stable kernel/user ABI. Wrappers MUST NOT depend on kernel-private structs, interrupt frame layout details, scheduler internals, VFS internals, or x86_64 backend implementation headers beyond the explicitly documented register convention and shared ABI constants.

#### Scenario: wrapper 构建不包含内核私有实现
- **WHEN** userland libc wrappers are built for freestanding user programs
- **THEN** their public and private userland includes MUST resolve through freestanding-safe user ABI headers
- **AND** wrapper code MUST NOT include kernel-only process, VFS, interrupt, scheduler, or architecture backend implementation headers

#### Scenario: wrapper 只暴露已实现 syscall
- **WHEN** a libc header declares a syscall wrapper or POSIX-like function
- **THEN** the declared interface MUST correspond to an implemented and specified BigOS bounded syscall or libc behavior
- **AND** the declaration MUST NOT imply complete POSIX libc, hosted stdio, dynamic loading, threads, locale, or unsupported filesystem semantics

### Requirement: 用户态公共头文件 ABI 边界
BigOS userland public headers SHALL expose only the minimal constants, types, syscall wrappers, errno values, and libc declarations needed by the current bounded userland subset. Header organization MUST keep kernel-private implementation details out of user programs while preserving a stable freestanding include surface.

#### Scenario: 头文件只暴露 bounded subset
- **WHEN** a simple static C user program includes BigOS userland public headers
- **THEN** it MUST see declarations for supported wrappers and libc helpers
- **AND** it MUST NOT see declarations for unsupported POSIX families, internal kernel structs, backend-specific descriptor state, or unimplemented runtime facilities

#### Scenario: errno 来源保持一致
- **WHEN** kernel syscall code and userland libc wrappers reference errno constants
- **THEN** they MUST use a shared stable errno value source or a generated equivalent proven consistent with that source
- **AND** userland MUST NOT maintain a divergent errno numbering table

### Requirement: 已使用公共声明先补规格再隐藏
BigOS SHALL treat declarations already used by bundled user programs, smoke programs, crt0, or libc internals as existing userland ABI surface until they are explicitly reviewed. If such a declaration lacks precise OpenSpec coverage, the implementation work MUST first add or update the corresponding specification before retaining, moving, renaming, or hiding that declaration.

#### Scenario: 盘点发现已使用但规格不足的声明
- **WHEN** the header audit finds an implemented declaration used by bundled userland but only covered by broad or implicit requirements
- **THEN** the change MUST add explicit bounded specification coverage for that declaration before changing its visibility
- **AND** the implementation MUST NOT silently hide the declaration if doing so breaks an existing shell, smoke, libc, or packaged user-program path

#### Scenario: 当前 BigOS-specific 声明被显式归类
- **WHEN** implementation audits declarations such as `wait_status`, `bigos_readdir`, `brk_raw`, `mmap_anon`, `time_now`, `get_tick`, raw `syscall0`-`syscall6`, or non-minimum string helpers such as `strchr`
- **THEN** each declaration MUST be classified as public bounded ABI, libc-internal-only helper, compatibility umbrella export, or removal candidate
- **AND** public or compatibility exports MUST have matching specification and documentation before the task is complete

### Requirement: raw syscall primitive 暴露受限
BigOS userland libc MAY keep raw `syscall0` through `syscall6` primitives for libc-internal or explicitly documented BigOS-specific use, but those primitives MUST be treated as low-level ABI helpers rather than POSIX-like portable APIs. Their declarations MUST remain freestanding-safe, follow the documented register ABI exactly, and avoid implying support for arbitrary unsupported syscalls.

#### Scenario: libc 内部通过 raw primitive 调用 syscall
- **WHEN** libc wrappers call raw syscall primitives
- **THEN** the raw primitive MUST place the syscall number and arguments in the documented registers and return the raw kernel value from `rax`
- **AND** higher-level wrappers MUST remain responsible for errno translation unless the primitive is explicitly documented as returning raw kernel values

#### Scenario: raw primitive 不扩大公共兼容承诺
- **WHEN** user-facing headers expose raw syscall primitives directly or through an umbrella header
- **THEN** documentation MUST describe them as BigOS-specific low-level helpers
- **AND** the exposure MUST NOT imply POSIX `syscall(2)` compatibility, broad syscall stability, hosted libc support, or permission to bypass documented wrapper semantics for unsupported operations

### Requirement: BigOS-specific helper 声明保持有界
BigOS userland libc SHALL document and constrain BigOS-specific helper declarations that are not standard C/POSIX names but are part of the current bounded userland, including process wait status helpers, minimal directory enumeration, raw break query, restricted anonymous mapping, monotonic tick query, and wall-clock query helpers.

#### Scenario: helper 声明与能力边界匹配
- **WHEN** headers expose a BigOS-specific helper for wait status, directory enumeration, heap/mapping, tick, or wall-clock behavior
- **THEN** the helper MUST map to an implemented syscall or libc behavior already covered by the relevant bounded capability
- **AND** its documentation MUST state the bounded return value, errno behavior, and non-goals where they differ from POSIX or hosted libc

#### Scenario: helper 缺少规格时不新增使用面
- **WHEN** a BigOS-specific helper lacks explicit specification or documentation
- **THEN** new user programs MUST NOT expand dependence on that helper until the specification is added
- **AND** existing uses MAY remain only as compatibility or smoke consumers while the boundary cleanup resolves the declaration

### Requirement: libc ABI 文档与头文件一致
BigOS SHALL keep userland libc documentation, public headers, and wrapper behavior aligned for the supported bounded subset.

#### Scenario: 文档描述已声明接口
- **WHEN** docs describe a userland libc wrapper, errno behavior, or public header
- **THEN** the described interface MUST be declared in the userland public headers and implemented in userland libc
- **AND** unsupported behavior MUST be documented as a non-goal rather than exposed as a declaration

#### Scenario: 英中镜像保持同一 ABI 事实
- **WHEN** userland libc ABI documentation is updated under `docs/en`
- **THEN** the corresponding `docs/zh` mirror MUST be updated with the same syscall wrapper, errno, and bounded subset facts

### Requirement: Minimal signal header and wrappers
The BigOS user libc SHALL expose a bounded `signal.h` surface for installed user programs, including supported signal constants, `sigset_t`, `struct sigaction`, `sigaction`, and `sigprocmask`, backed by the existing signal syscalls.

#### Scenario: User program installs a signal handler
- **WHEN** a static user program includes `signal.h` and calls `sigaction` for a supported signal
- **THEN** the wrapper invokes the BigOS syscall ABI, returns zero on success, and preserves errno translation on failure

#### Scenario: User program changes signal mask
- **WHEN** a static user program calls `sigprocmask` with a supported mask operation
- **THEN** the wrapper updates the process signal mask through the existing syscall and optionally writes the previous mask

### Requirement: Bounded wait wrappers
The BigOS user libc SHALL expose bounded `wait(int *status)` and `waitpid(pid_t pid, int *status, int options)` wrappers while preserving a BigOS-specific compatibility wrapper for callers that need the raw existing wait shape.

#### Scenario: wait waits for any child
- **WHEN** a user program calls `wait` with a status pointer
- **THEN** libc waits for any child, returns the reaped child pid, and writes the raw bounded child status

#### Scenario: waitpid rejects unsupported options
- **WHEN** a user program calls `waitpid` with unsupported options
- **THEN** libc returns `-1`, sets errno to a deterministic error, and does not request a child reap from the kernel

### Requirement: Bounded time and error text interfaces
The BigOS user libc SHALL expose `time`, `strerror`, and `perror` or equivalent bounded C/POSIX-like interfaces without requiring hosted libc, locale, dynamic allocation, or complete stdio semantics.

#### Scenario: time returns kernel seconds
- **WHEN** a user program calls `time` with a non-null output pointer
- **THEN** libc returns the current BigOS wall-clock seconds and stores the same value through the pointer

#### Scenario: perror writes deterministic stderr text
- **WHEN** a user program calls `perror` after a wrapper sets errno
- **THEN** libc writes the optional prefix, separator, stable error text, and newline to fd 2 using the bounded userland output path

### Requirement: bounded libc foundation 标准 C 库基础子集边界

BigOS 用户态 libc SHALL 将 bounded libc foundation 的交付边界定义为服务简单静态 C 程序的 bounded standard C library foundation。该基础子集 MUST 覆盖 freestanding public headers、统一 errno mirror、syscall wrapper、字符串/内存函数、基础 stdlib、最小 stdio/error reporting、只读环境访问、文件/进程 wrapper、BigOS-specific helper 分类和行为验证。该基础子集 MUST NOT 声明动态链接、共享库、动态 loader、完整 hosted libc、完整 POSIX libc、线程、locale、完整 `FILE` 流、完整 terminal/job-control/session/process-group、broad file-backed `mmap` 或广泛 POSIX 兼容。

#### Scenario: 静态 C 程序消费 libc 基础子集

- **WHEN** 一个 BigOS 静态 C 用户程序只包含 bounded libc foundation public headers 并调用已支持 libc 接口
- **THEN** 该程序 MUST 能在 freestanding 用户态构建中解析声明并静态链接到 BigOS libc
- **AND** 该程序 MUST NOT 需要宿主 libc、动态 loader、共享库、线程 runtime、locale 或完整 POSIX runtime

#### Scenario: 文档不扩大兼容声明

- **WHEN** 文档、规格、头文件注释或用户程序说明描述 bounded libc foundation
- **THEN** 它们 MUST 将能力描述为 bounded C library subset 或 bounded POSIX-like wrapper subset
- **AND** MUST NOT 暗示 BigOS 已支持完整 POSIX libc、动态链接、共享库或广泛 POSIX 兼容

### Requirement: 用户态公共声明分类与收敛

BigOS 用户态 libc SHALL 对 public headers、umbrella header、raw syscall primitive、BigOS-specific helper 和 bundled user program 使用的声明进行分类。每个公开声明 MUST 被归类为标准 C 子集接口、POSIX-like bounded wrapper、BigOS-specific public helper、compatibility umbrella export 或 libc-internal-only helper。任一 public 或 compatibility export MUST 有对应实现、规格和文档边界；任一未实现接口 MUST NOT 被 public headers 声明为支持。

#### Scenario: 公开声明有明确分类

- **WHEN** 审查 bounded libc foundation 后的用户态 libc 头文件集合
- **THEN** 每个公开函数、类型、常量或 raw helper MUST 能映射到一个明确分类
- **AND** 该分类 MUST 与实现、规格和文档描述一致

#### Scenario: 未实现接口不被声明

- **WHEN** 审查 public headers 或 umbrella header
- **THEN** 头文件 MUST NOT 声明没有实现或没有规格覆盖的 hosted/POSIX 接口
- **AND** 头文件 MUST NOT 通过宿主系统头、隐式 prototype 或 compatibility export 扩大当前支持范围

### Requirement: bounded libc foundation 高价值 C 库函数

BigOS 用户态 libc SHALL 优先补齐或稳定简单静态程序高频使用的 C 库函数。bounded libc foundation 第一批接口 MUST include `calloc`、`realloc`、`strtol`、`atoi` 和 bounded `snprintf`。新增或整理的函数 MUST 保持 freestanding-safe，MUST 不依赖宿主 runtime，MUST 在有界输入下具有确定性行为，并且 MUST 在失败时遵循接口约定而不破坏调用者已有状态。`calloc` MUST 检查乘法溢出并返回清零内存；`realloc` MUST 在失败时保留原块有效；`strtol` MUST 支持有界整数解析、base 处理和 end pointer；`atoi` MUST 作为十进制简化 wrapper；`snprintf` MUST 遵守输出缓冲区边界并复用 bounded formatter。

#### Scenario: 新增函数具备实现和规格

- **WHEN** bounded libc foundation 向 public headers 新增一个 C 库函数声明
- **THEN** BigOS 用户态 libc MUST 提供对应实现和 OpenSpec 覆盖
- **AND** 该接口 MUST 有可由构建检查、用户态 smoke 或代表性用户程序观察的行为

#### Scenario: 失败不破坏已有状态

- **WHEN** 新增或整理的分配、转换、格式化或 wrapper 函数遇到有界资源不足、无效参数或内核返回错误
- **THEN** 该函数 MUST 按接口约定返回失败哨兵或错误结果
- **AND** MUST NOT 破坏既有有效分配块、调用者缓冲区中已承诺保留的内容、或 unrelated fd/process state

#### Scenario: 第一批接口可由简单程序使用

- **WHEN** 一个 bounded libc foundation 静态 C 程序包含对应 public headers 并调用 `calloc`、`realloc`、`strtol`、`atoi` 或 `snprintf`
- **THEN** 程序 MUST 能在 freestanding 用户态构建中解析声明并链接实现
- **AND** 每个接口 MUST 有 bounded 行为说明和用户态验证覆盖

### Requirement: bounded stdio 与错误报告保持 fd-backed

BigOS 用户态 libc SHALL 在 bounded libc foundation 中保持 stdio 为 fd-backed bounded helper。`stdin`、`stdout`、`stderr` MAY 使用最小 opaque `FILE` 表示，但 public behavior MUST 限定在 standard streams、`read`/`write` wrapper、格式化输出和 deterministic error reporting。bounded libc foundation bounded formatter MUST support the existing minimal formats plus common width and integer/pointer forms, at least `%u`、`%p`、`%ld`、`%lu` and `%zu`, and MUST share behavior across `printf`、`fprintf(stderr, ...)` and bounded `snprintf` where applicable. bounded libc foundation stdio MUST NOT require `fopen`、`fclose`、`fread`、`fwrite`、`fflush`、完整 buffering、locale、宽字符、浮点 formatting、完整 flags 或完整 precision。

#### Scenario: stdout stderr 输出可观察

- **WHEN** 用户程序通过 `printf`、`fprintf(stderr, ...)`、`puts`、`putchar` 或 `perror` 输出文本
- **THEN** 输出 MUST 经 fd 1 或 fd 2 到达当前 fd/VFS/console/shell 可观察路径
- **AND** 该行为 MUST 不依赖完整 hosted `FILE` 流、动态分配、locale 或宿主 stdio runtime

#### Scenario: stdio 非目标不被暴露

- **WHEN** 审查 bounded libc foundation stdio 相关 public headers
- **THEN** headers MUST NOT 声明未实现的 hosted stream API 或完整 POSIX stdio 行为
- **AND** 任一新增 formatting 能力 MUST 有 bounded 行为说明和验证覆盖

#### Scenario: 常用格式由共享 formatter 支持

- **WHEN** 用户程序通过 `printf`、`fprintf(stderr, ...)` 或 `snprintf` 使用宽度、`%u`、`%p`、`%ld`、`%lu` 或 `%zu`
- **THEN** libc MUST 按 bounded libc foundation bounded formatter 规则产生确定性文本
- **AND** 该行为 MUST 不要求浮点、locale、宽字符、完整 flags 或完整 precision 支持

### Requirement: raw syscall primitive 不面向普通程序暴露

BigOS 用户态 libc SHALL 保留 `syscall0` through `syscall6` only as BigOS-specific documented low-level helpers for libc internals and explicitly opted-in low-level user programs. These primitives MUST NOT be exposed from the general umbrella header as recommended ordinary C program APIs. They MUST return raw kernel values, MUST NOT perform errno translation, MUST NOT imply POSIX `syscall(2)` compatibility, and MUST NOT imply that arbitrary syscall numbers are stable or supported.

#### Scenario: 普通程序不经 umbrella header 看到 raw syscall

- **WHEN** 一个普通 bounded libc foundation 静态 C 程序只包含 general libc umbrella header 或标准 C/POSIX-like public headers
- **THEN** 程序 MUST NOT be encouraged to call raw `syscall0` through `syscall6` as ordinary libc APIs
- **AND** 程序 MUST use documented wrappers for errno-translated process、fd、time、signal and filesystem operations

#### Scenario: 低层 helper 明确返回 raw 值

- **WHEN** libc 内部或明确 opt-in 的 BigOS-specific 程序调用 raw syscall primitive
- **THEN** primitive MUST follow the documented register ABI and return the raw kernel `rax` value
- **AND** caller-visible documentation MUST state that errno translation is not performed by the primitive

### Requirement: bounded DIR* 风格目录枚举

BigOS 用户态 libc SHALL provide a `DIR*`-style bounded directory enumeration wrapper for simple static C programs while explicitly documenting it as not complete POSIX directory traversal. The wrapper set MUST provide open/read/close-style directory consumption using the current bounded VFS directory capability, MUST expose only fields backed by BigOS directory metadata, and MUST translate failures through userland errno. It MUST NOT claim support for complete `struct dirent`, ordering, cross-call snapshots, thread safety, `telldir`、`seekdir`、`rewinddir`、directory fd semantics, symlinks, mount namespaces, or full POSIX traversal behavior.

#### Scenario: 简单程序枚举目录

- **WHEN** 一个 bounded libc foundation 静态 C 程序通过 `DIR*` 风格 wrapper 打开并读取一个支持目录
- **THEN** libc MUST return bounded directory entries backed by the current BigOS directory metadata
- **AND** end-of-directory and failure MUST be distinguishable according to the documented wrapper and errno contract

#### Scenario: DIR* 非完整 POSIX

- **WHEN** 文档、headers 或 validation notes 描述 bounded libc foundation `DIR*` 风格 wrapper
- **THEN** 它们 MUST describe the interface as a BigOS bounded directory subset
- **AND** MUST NOT imply complete POSIX `opendir`/`readdir`/`closedir` semantics beyond the documented subset

### Requirement: bounded libc foundation 验证分层

BigOS SHALL 为 bounded libc foundation提供分层验证路径。验证 MUST 覆盖 public header 可构建性、errno 翻译、成功调用不改写 errno、字符串/内存例程、分配失败语义、stdio/error reporting、只读环境访问、文件/进程 wrapper 和至少一个代表性静态用户程序消费路径。依赖 QEMU、Bochs、x86_64 cross toolchain、显示/ROM 依赖或磁盘镜像配置的运行时检查 MAY 被跳过，但跳过记录 MUST 说明缺失条件、已运行替代检查和残余风险。

#### Scenario: libc subset smoke 覆盖核心行为

- **WHEN** bounded libc foundation subset runtime validation 在配置完整的 emulator 环境中运行
- **THEN** 验证 MUST 观察到至少一个静态用户程序的参数/环境读取、errno 翻译、stdout/stderr 输出、字符串/内存函数、堆分配和 wrapper 失败路径
- **AND** 结果 MUST 能由用户程序输出、退出状态、serial/log 输出或其他确定性低层信号判定

#### Scenario: 环境不可用时记录跳过

- **WHEN** 本地缺少 emulator、cross toolchain、显示/ROM 依赖、磁盘镜像配置或 timeout oracle
- **THEN** 对应运行时验证 MAY 被跳过
- **AND** 验证记录 MUST 明确缺失条件、已执行的替代检查和剩余风险
