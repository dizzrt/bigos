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
