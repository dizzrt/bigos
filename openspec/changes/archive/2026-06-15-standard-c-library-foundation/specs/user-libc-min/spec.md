## ADDED Requirements

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
