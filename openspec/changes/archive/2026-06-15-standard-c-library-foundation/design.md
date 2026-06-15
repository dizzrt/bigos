## Context

BigOS 当前已经有可运行的有界用户态：内核通过现有用户 ELF64 装载路径进入静态用户程序，`crt0` 将初始栈转换为 `main(argc, argv, envp)`，用户态 libc 通过 `int 0x80` wrapper 消费内核 syscall，并为 shell、PID-1 init、小型 `/bin/*` 工具和 userland smoke 提供最小 C 运行时支撑。

Stage 40 的问题不在于“没有 libc”，而在于现有 libc 表面来自多个阶段的增量补齐：部分接口是标准 C 风格，部分接口是 POSIX-like wrapper，部分接口是 BigOS-specific helper，部分能力只由 smoke 或 bundled tool 隐式依赖。这个 change 需要把它们整理成一个清晰的标准 C 库基础子集，并明确哪些能力属于当前承诺、哪些只是内部 helper、哪些必须留到后续阶段。

本设计影响用户态运行时和 libc，不改变 boot handoff、ELF 装载地址、页表布局、interrupt/syscall vector、磁盘布局、CR3 切换、内核调度或 VFS 内部 ABI。当前 runnable 目标仍是 x86_64 Legacy BIOS/MBR/exFAT 默认路径；UEFI runtime parity、SMP、多架构、动态链接和持久完整可写文件系统仍在本 change 范围外。

## Goals / Non-Goals

**Goals:**

- 建立 Stage 40 的 bounded standard C library foundation：public headers、errno、syscall wrapper、字符串/内存函数、stdlib、最小 stdio、环境访问、文件/进程 wrapper、BigOS-specific helper 和验证路径都有明确边界。
- 保持简单静态 C 程序为主要消费者，使 packaged tools 和 smoke 程序能更少依赖 raw syscall 或隐式内部声明。
- 让每个公开声明都能被归类为标准 C 子集、POSIX-like bounded wrapper、BigOS-specific public helper、compatibility umbrella export 或内部实现细节。
- 继续复用现有内核 syscall ABI 和统一 errno 来源，不新增 syscall 语义作为 Stage 40 的必要前提。
- 提供分层验证：头文件/构建一致性、用户态 libc 行为 smoke、代表性 shell/tool 组合行为，以及环境不可用时的明确跳过记录。

**Non-Goals:**

- 不实现动态链接、共享库、动态 loader、GOT/PLT/TLS、共享对象版本或文件映射式库加载。
- 不声明完整 POSIX libc、完整 hosted libc、广泛 POSIX 兼容、完整 `FILE` 流、locale、线程、宽字符或浮点格式化。
- 不引入完整 terminal、session、process group、job control、termios 或完整 POSIX shell。
- 不要求 broad file-backed `mmap`、持久完整可写文件系统、async I/O、SMP 或广泛 storage/device support。
- 不修改 x86_64 boot 地址、linker 地址、page-table self mapping、interrupt vector、syscall vector、磁盘布局或内核 ABI。

## Decisions

### Decision: 以“静态 C 程序 libc 子集”为 Stage 40 交付边界

Stage 40 的主要交付对象是静态链接的简单 C 程序，而不是通用 Unix/Linux 用户态程序。libc 接口应优先支持 bundled tools、shell 诊断、文件 I/O、进程交互、字符串/内存操作和错误报告。

备选方案是直接追求完整 POSIX libc 或动态 libc。该方案会立即要求 loader、VM、FS、terminal/process、locale/thread 等多个未成熟契约同步稳定，容易把 Stage 40 变成跨系统大迁移，因此不采用。

```text
static user program
        |
        v
freestanding public headers
        |
        v
bounded libc implementation
        |
        v
stable syscall ABI + shared constants
        |
        v
existing kernel process/fd/VFS/time/signal subset
```

### Decision: 公开头文件先做归类和收敛，再补接口

实现前先盘点 `user/libc/include`、umbrella header、raw syscall primitive、BigOS-specific helper 和 bundled user programs 的包含关系。每个声明必须归入以下类别之一：

- 标准 C 子集接口，例如字符串、内存、stdlib、最小 stdio 和环境读取。
- POSIX-like bounded wrapper，例如 `read`、`write`、`open`、`close`、`fork`、`execve`、`waitpid`、`pipe`、`dup2`、`chdir`、`getcwd`。
- BigOS-specific public helper，例如 raw wait status helper、最小目录枚举、restricted anonymous mapping、tick/time helper。
- compatibility umbrella export，用于避免破坏现有用户程序，但必须有文档说明。
- libc-internal-only helper，不再扩大 public surface。

备选方案是按缺失函数逐个添加。该方案实现快，但会继续扩大不清晰 ABI 面，后续难以判断哪些接口可移植、哪些是 BigOS-specific，因此不采用。

### Decision: errno 和 syscall wrapper 继续以共享 ABI 常量为唯一来源

libc wrapper 必须继续把内核负 errno 翻译为用户态正 errno 并返回接口约定的失败哨兵；成功调用不得清零或改写 `errno`。新增或整理的 wrapper 不应绕过统一 errno 来源，也不应引入用户态独立错误码表。

备选方案是在用户态维护一套 POSIX errno 映射表。该方案可能更接近 hosted libc 习惯，但会引入与内核错误码漂移的风险，因此不采用。

### Decision: stdio 保持 fd-backed bounded helper，不进入完整 FILE 流

Stage 40 可增强 `printf`/`fprintf`/`perror` 等诊断能力，也可以补齐小型工具高频使用的格式能力，但 `FILE` 仍应只作为 standard streams 的最小 opaque 表示。`fopen`、`fclose`、`fread`、`fwrite`、`fflush`、seekable buffered stream、locale 和宽字符不作为本阶段目标。

备选方案是实现 hosted stdio。该方案会与 VFS 文件更新语义、缓冲刷新、错误状态、stdin 交互和未来持久存储强耦合，更适合作为后续独立成熟化项目。

### Decision: Stage 40 第一批 libc 接口按高实用、低系统牵引选择

第一批必须接口包括 `calloc`、`realloc`、`strtol`、`atoi` 和 bounded `snprintf`。`calloc` 必须处理乘法溢出并返回清零内存；`realloc` 必须保持失败不破坏原块；`strtol` 作为推荐的数字解析接口；`atoi` 作为 `strtol` 的简化兼容 wrapper；`snprintf` 必须复用 bounded formatter，遵守缓冲区边界和截断返回约定。

备选方案是只补 `calloc`/`realloc` 并推迟转换和 formatting。该方案会让 shell tools 和 packaged programs 继续保留手写解析/格式化逻辑，不利于 Stage 40 提升静态 C 程序可移植性，因此不采用。完整 hosted `snprintf`、浮点、locale、宽字符和完整 flags/precision 仍不属于本阶段目标。

### Decision: raw syscall primitive 只作为 BigOS-specific documented helper 保留

`syscall0` 到 `syscall6` 不应作为 umbrella header 面向普通用户程序的推荐 API 暴露。它们可以保留给 libc 内部和明确 include BigOS-specific low-level header 的调试/实验程序，但必须文档化为 raw ABI helper：返回内核原始值，不负责 errno 翻译，不等价于 POSIX `syscall(2)`，也不承诺任意 syscall 稳定可用。

备选方案是继续从 umbrella header 暴露 raw primitive。该方案方便实验，但会鼓励普通程序绕过 libc wrapper 和 errno 契约，扩大未稳定 syscall ABI 的使用面，因此不采用。

### Decision: 目录枚举采用接近 POSIX 的 DIR* 风格但声明为非完整 POSIX

Stage 40 引入 `DIR*` 风格的 bounded directory enumeration wrapper，使简单 C 程序可以通过更熟悉的 `opendir`/`readdir`/`closedir` 形态消费现有目录枚举能力。该接口必须明确是 BigOS bounded subset：不承诺完整 `struct dirent`、排序、跨调用快照、线程安全、`telldir`/`seekdir`、`rewinddir`、目录 fd 语义、symlink 或完整 POSIX 目录遍历。

备选方案是继续只保留 BigOS-specific 目录枚举命名。该方案边界最安全，但降低小程序移植收益；在 Stage 40 目标是建立更清晰 C 库基础的前提下，选择引入受限 `DIR*` 风格 wrapper，并用规格和文档约束非目标。

### Decision: printf family 增加常用宽度和整数/指针格式

Stage 40 的 bounded formatter 应在现有 `%s`、`%d`、`%x`、`%c`、`%%` 基础上增加常用宽度和整数/指针格式，至少覆盖 `%u`、`%p`、`%ld`、`%lu`、`%zu`，并支持简单字段宽度用于工具输出对齐。该能力必须适用于 `printf`、`fprintf(stderr, ...)` 和 bounded `snprintf` 的共享 formatter，避免多套格式化行为分叉。

备选方案是只稳定现有最小格式集合。该方案实现风险最低，但不足以支撑更实用的 shell tools、诊断输出和指针/大小值展示，因此不采用。完整 flags、precision、浮点、locale 和宽字符仍明确留到后续阶段。

### Decision: allocator 以确定性失败语义为优先

Stage 40 可以补齐 `calloc`、`realloc` 等高价值接口，但要求仍是有界、可预测、失败不破坏既有块。不得把当前 allocator 描述成通用高性能、线程安全或完整 coalescing allocator。

备选方案是重写通用 malloc。该方案会增加 VM 和 fragmentation 复杂度，并可能掩盖 Stage 40 的主要目标，因此不作为首要路径。

### Decision: 验证围绕可观察用户态行为，而不是外部兼容测试套件

验证应覆盖三类信号：public header 可构建、libc subset 行为可由用户程序输出/退出状态观察、shell/tool 组合路径能消费 libc wrapper。QEMU/Bochs、cross toolchain 或本地镜像不可用时，运行时验证可以跳过，但必须记录缺失条件、替代检查和残余风险。

备选方案是直接引入大规模 POSIX/libc compatibility suite。该方案会制造大量当前明确非目标的失败，不利于 bounded subset 收敛，因此不采用。

## Risks / Trade-offs

- Scope creep 到完整 POSIX libc -> 通过 proposal/spec 明确非目标，新增声明必须有实现、规格和文档边界。
- 头文件过早承诺未实现接口 -> 先做 header audit 和声明分类，未实现接口不得暴露为 public support。
- stdio 增强牵出完整 `FILE` 语义 -> 将 Stage 40 stdio 限定为 fd 0/1/2 与 bounded formatting/error reporting。
- allocator 改动破坏现有用户程序 -> 优先添加行为 smoke，要求失败不破坏既有块，保留 `free(NULL)` 无副作用。
- wrapper 整理改变 errno 行为 -> 每个新增或调整 wrapper 都必须覆盖成功不改写 errno、失败设置正 errno 和返回哨兵。
- 文档宣称过宽兼容 -> docs 和 OpenSpec 必须使用 bounded C library subset / POSIX-like subset 表述，不使用广泛 POSIX compatibility 表述。
- 环境依赖导致验证不可复现 -> 分层记录构建检查、静态检查、emulator runtime smoke 和跳过原因。

## Migration Plan

1. 盘点当前用户态 libc public headers、umbrella header、raw syscall primitive、BigOS-specific helper 和 bundled user program 使用点。
2. 更新 OpenSpec delta，明确 Stage 40 C 库基础子集、crt0 静态入口边界和 POSIX-like 子集兼容声明边界。
3. 按声明分类整理头文件，先补规格覆盖，再移动、隐藏或保留声明。
4. 补齐高价值 libc 函数和 wrapper，优先覆盖字符串/内存、stdlib、allocator、stdio/error reporting、文件/进程 wrapper。
5. 更新或新增用户态 libc subset smoke 和代表性 packaged tool 使用路径。
6. 同步 docs/en 与 docs/zh 的用户态 libc 边界说明，避免声明完整 POSIX libc 或动态链接支持。
7. 如果某项接口整理破坏现有 bundled userland，回滚到 compatibility umbrella export，并记录后续清理任务。

## Open Questions

- 是否需要在 Stage 40 第一批之外追加 `strtoul`、`snprintf` precision、`%o` 或更多 length modifier，取决于 packaged tools 和 smoke 的实际消费需求。
- `DIR*` 风格 wrapper 的最小 `struct dirent` 字段集合需要在实现前根据现有 VFS 目录项能力最终确认。
