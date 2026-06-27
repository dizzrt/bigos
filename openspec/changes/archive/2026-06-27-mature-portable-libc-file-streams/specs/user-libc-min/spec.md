## MODIFIED Requirements

### Requirement: 最小 stdio 行为可观察且有限

BigOS 用户态 libc SHALL 提供基于 fd 0/1/2 和 `read`/`write` wrapper 的 stdio helper，并 MAY 在有界范围内将其升级为缓冲 `FILE` 流子集。`stdin`、`stdout`、`stderr` MUST 表示为绑定 fd 0/1/2 的标准流。`putchar`、`puts`、`printf` 和 `fprintf(stderr, ...)` MUST 通过当前 fd/VFS/console 路径产生可观察输出；`printf`/`fprintf` MUST 至少支持 `%s`、`%d`、`%x`、`%c`、`%%`。当 libc 暴露有界缓冲 `FILE` 流时，其语义 MUST 由 `bounded-file-streams` 能力定义；stdio MUST NOT 引入 locale、宽字符、浮点格式化、`scanf` 家族或完整 hosted stdio runtime。

#### Scenario: 最小 printf 输出支持格式

- **WHEN** 用户程序调用 `printf` 并使用 `%s`、`%d`、`%x`、`%c` 或 `%%`
- **THEN** libc MUST 将格式化文本写入 stdout
- **AND** 输出 MUST 能经当前控制台、shell 输出或串口日志路径观察

#### Scenario: fprintf stderr 输出错误

- **WHEN** 用户程序调用 `fprintf(stderr, ...)` 并使用 `%s`、`%d`、`%x`、`%c` 或 `%%`
- **THEN** libc MUST 将格式化文本写入 stderr 对应的 fd 2
- **AND** 输出 MUST 能经当前错误输出、控制台或串口日志路径观察

#### Scenario: stdio 缓冲为有界子集

- **WHEN** 用户程序使用 `putchar`、`puts`、`printf`、`fprintf` 或有界缓冲 `FILE` 流接口
- **THEN** 输出 MUST 经 fd/write 语义最终落盘，缓冲行为 MUST 遵循 `bounded-file-streams` 能力定义
- **AND** MUST NOT 依赖 locale、宽字符、浮点格式化或宿主 stdio runtime

### Requirement: bounded stdio 与错误报告保持有界

BigOS 用户态 libc SHALL 保持 stdio 为有界 helper，可基于 fd 直接输出，亦可经有界缓冲 `FILE` 流实现。`stdin`、`stdout`、`stderr` MUST 表示为绑定 fd 0/1/2 的标准流，public behavior MUST 限定在标准流、`read`/`write` 语义、格式化输出、有界缓冲 `FILE` 流子集与 deterministic error reporting。bounded formatter MUST support the existing minimal formats plus common width and integer/pointer forms, at least `%u`、`%p`、`%ld`、`%lu` and `%zu`, and MUST share behavior across `printf`、`fprintf` and bounded `snprintf` where applicable。当 libc 暴露 `fopen`/`fclose`/`fread`/`fwrite`/`fflush` 等缓冲流接口时，其语义 MUST 由 `bounded-file-streams` 能力定义；stdio MUST NOT require locale、宽字符、浮点 formatting、完整 flags、完整 precision、`scanf` 家族或完整 hosted stdio runtime。

#### Scenario: stdout stderr 输出可观察

- **WHEN** 用户程序通过 `printf`、`fprintf(stderr, ...)`、`puts`、`putchar` 或 `perror` 输出文本
- **THEN** 输出 MUST 经 fd 1 或 fd 2 到达当前 fd/VFS/console/shell 可观察路径
- **AND** 该行为 MUST 不依赖 locale 或宿主 stdio runtime

#### Scenario: stdio 非目标不被暴露

- **WHEN** 审查 stdio 相关 public headers
- **THEN** headers MUST NOT 声明未实现的 hosted stream API、`scanf` 家族或完整 POSIX stdio 行为
- **AND** 任一新增 formatting 或缓冲流能力 MUST 有 bounded 行为说明和验证覆盖

#### Scenario: 常用格式由共享 formatter 支持

- **WHEN** 用户程序通过 `printf`、`fprintf(stderr, ...)` 或 `snprintf` 使用宽度、`%u`、`%p`、`%ld`、`%lu` 或 `%zu`
- **THEN** libc MUST 按 bounded formatter 规则产生确定性文本
- **AND** 该行为 MUST 不要求浮点、locale、宽字符、完整 flags 或完整 precision 支持

### Requirement: portable libc subset public header maturity

BigOS 用户态 libc SHALL extend the existing minimal libc public header boundary to support the portable libc subset. Headers MUST expose implemented and specified declarations for common small-program use, including supported standard C subset helpers, bounded `time.h` and `assert.h` declarations, POSIX-like bounded wrappers, BigOS-specific helpers where explicitly opted in, a bounded buffered `FILE` stream subset as defined by the `bounded-file-streams` capability, and compatibility exports where needed. Headers MUST NOT expose unsupported hosted libc, complete POSIX libc, dynamic loader, locale, thread, wide-character, complete hosted stdio (such as the `scanf` family, wide streams, `tmpfile`, `fmemopen`, complete `setvbuf` strategies, or complete `fpos_t` positioning), complete timezone/calendar APIs, or unimplemented syscall behavior.

#### Scenario: portable headers expose supported declarations

- **WHEN** 一个简单静态 C 程序包含 portable libc subset headers
- **THEN** it MUST see declarations for implemented supported helpers, wrappers, and the bounded buffered `FILE` stream subset
- **AND** those declarations MUST match the linked BigOS user libc implementation

#### Scenario: unsupported hosted declarations not exposed

- **WHEN** 审查用户态 libc public headers
- **THEN** headers MUST NOT declare unsupported hosted or complete POSIX libc interfaces (including the `scanf` family or wide streams) as available
- **AND** any compatibility export MUST have a documented bounded behavior or a documented migration path

## ADDED Requirements

### Requirement: 扩充的可移植 string/stdlib helper 子集

BigOS 用户态 libc SHALL 在有界范围内扩充面向可移植小程序的 `string.h`/`stdlib.h` helper。扩充内容 MUST 以实际新增消费者或验证程序为准，可包括 `memcmp`、`strcat`/`strncat`、`strspn`/`strcspn`/`strpbrk`、显式可重入的 `strtok_r`，以及 `abs`/`labs`、`strtoll`/`strtoull`、使用显式比较器的 `qsort`/`bsearch`。这些 helper MUST 遵循标准 C 语义、无 locale 依赖、无隐藏全局态（tokenizer 使用调用方提供的 `saveptr`），并复用既有数值转换的 errno/溢出契约。该子集 MUST NOT 引入隐藏全局态的 `strtok`、locale-aware 比较、宽字符或浮点解析，未纳入项保持显式非支持边界。

#### Scenario: 扩充 helper 遵循标准 C 语义

- **WHEN** 用户程序调用受支持的扩充 `string.h`/`stdlib.h` helper
- **THEN** libc MUST 按标准 C 语义返回结果且不依赖 locale 或宿主 runtime
- **AND** `qsort`/`bsearch` MUST 使用调用方提供的比较器，`strtok_r` MUST 使用调用方提供的 `saveptr` 而非隐藏全局态

#### Scenario: 宽度扩展转换沿用既有契约

- **WHEN** 用户程序调用 `strtoll`/`strtoull` 解析有界输入或溢出输入
- **THEN** libc MUST 复用既有 `strtol`/`strtoul` 的 base 选择、end 指针与 errno/溢出契约并扩展到 `long long` 宽度
- **AND** MUST NOT 读取超出 NUL 终止输入的范围

#### Scenario: 未纳入 helper 不被声明为已支持

- **WHEN** 审查扩充 helper 的 public headers
- **THEN** headers MUST NOT 把隐藏全局态 `strtok`、locale-aware helper 或浮点解析声明为已支持
- **AND** 这些 helper 的未来暴露 MUST 伴随显式规格与验证

### Requirement: 扩充的 ctype 分类 helper

BigOS 用户态 libc SHALL 在有界范围内扩充 ASCII/C-locale-style `ctype` 分类 helper，可包括 `isxdigit`、`ispunct`、`iscntrl`、`isgraph`、`isblank`。这些 helper MUST 使用确定性 ASCII/C-locale-style 行为，覆盖 unsigned char 取值与 EOF-style 输入，且 MUST NOT 依赖 locale 数据库、多字节、Unicode 或宽字符。

#### Scenario: 扩充 ctype 分类确定

- **WHEN** 用户程序对 ASCII 字符、非 ASCII 字节值或 EOF-style 输入调用扩充 `ctype` helper
- **THEN** libc MUST 按文档化 ASCII/C-locale-style 规则返回确定性结果
- **AND** MUST NOT 查询 locale、Unicode 表或动态 runtime 状态

### Requirement: 标准 freestanding 头复用工具链

BigOS 用户态 libc SHALL 使可移植小程序所需的标准 freestanding 头子集（至少 `stddef.h`、`stdint.h`、`limits.h`、`stdbool.h`、`stdarg.h`）在 BigOS 用户构建中可解析，并 MUST 直接复用交叉工具链在 `-ffreestanding` 下提供的版本。本仓库 `user/libc/include` MUST NOT 为这些标准头提供副本。`user/libc/include/sys/types.h` 中的 `size_t` 与 `NULL` MUST 改为转引工具链 `<stddef.h>`，仅保留 BigOS 自有类型（如 `ssize_t`/`off_t`/`mode_t`/`pid_t`），以避免与工具链头的重复定义冲突。该头子集 MUST NOT 引入 hosted runtime 依赖、动态初始化或宿主 OS 服务。

#### Scenario: 标准头由工具链解析

- **WHEN** 一个可移植小程序包含 `stddef.h`/`stdint.h`/`limits.h`/`stdbool.h`/`stdarg.h` 并使用其标准类型与宏
- **THEN** 构建 MUST 经交叉工具链 freestanding 头解析这些声明
- **AND** 本仓库 MUST NOT 为这些标准头提供用户态副本

#### Scenario: sys/types.h 转引消除重复定义

- **WHEN** 一个程序同时包含工具链 `<stddef.h>` 与 BigOS `sys/types.h`
- **THEN** `size_t`/`NULL` MUST 来自工具链 `<stddef.h>` 的转引而非本仓库重复定义
- **AND** 构建 MUST NOT 因 `size_t`/`NULL` 重复 typedef 或宏冲突而失败
