## ADDED Requirements

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
