# portable-libc-subset Specification

## Purpose
TBD - created by archiving change mature-portable-libc-subset. Update Purpose after archive.
## Requirements
### Requirement: portable libc subset 边界

BigOS SHALL define a portable libc subset for simple statically linked C programs on top of the existing bounded userland. This subset MUST cover freestanding-safe public headers, ASCII/C-locale-style `ctype` behavior, bounded `time.h` and `assert.h` public header behavior, bounded numeric conversion, bounded formatter behavior, a bounded buffered `FILE` stream subset as defined by the `bounded-file-streams` capability, deterministic error reporting, POSIX-like wrapper consumption, and representative program validation. The subset MUST NOT claim complete POSIX libc, hosted libc, dynamic linking, shared libraries, locale, threads, wide characters, complete hosted stdio (the `scanf` family, wide streams, `tmpfile`, `fmemopen`, complete `setvbuf` strategies, or complete `fpos_t` positioning), complete calendar/timezone APIs, broad `mmap`, async I/O, SMP, or broad POSIX compatibility.

#### Scenario: 简单可移植程序只依赖 portable subset

- **WHEN** 一个简单静态 C 程序只包含 portable libc subset 的 public headers 并调用已规格化接口
- **THEN** 该程序 MUST 能通过 BigOS freestanding 用户程序构建路径解析声明并静态链接
- **AND** 该程序 MUST NOT 需要宿主 libc、动态 loader、共享库、线程 runtime、locale 或完整 POSIX runtime

#### Scenario: 文档不扩大兼容声明

- **WHEN** 文档、OpenSpec、头文件注释或用户程序说明描述 portable libc subset
- **THEN** 它们 MUST 将能力描述为 bounded C library subset 或 bounded POSIX-like wrapper subset
- **AND** MUST NOT 暗示 BigOS 支持完整 POSIX libc、完整 hosted libc、动态链接、共享库或广泛 POSIX 兼容

#### Scenario: 缓冲 FILE 流为有界子集

- **WHEN** 文档、头文件或用户程序说明描述 portable subset 的 `FILE` 流能力
- **THEN** 它们 MUST 将其描述为 `bounded-file-streams` 定义的有界缓冲流子集
- **AND** MUST NOT 暗示完整 hosted stdio、`scanf` 家族、宽流、locale 或完整 `fpos_t` 定位

### Requirement: portable public headers 可分类且可构建

BigOS portable libc subset SHALL expose only implemented and specified public declarations through freestanding-safe headers. Every public declaration MUST be classified as standard C subset, POSIX-like bounded wrapper, BigOS-specific public helper, compatibility export, or libc-internal-only helper. Public headers MUST NOT depend on host libc headers, host OS services, dynamic initialization, exceptions, RTTI, threads, or shared libraries.

#### Scenario: public declaration 有明确分类

- **WHEN** 审查 portable libc subset 的 public header 集合
- **THEN** 每个公开函数、类型、宏、常量或 helper MUST 能映射到一个明确分类
- **AND** 该分类 MUST 与实现、规格和文档边界一致

#### Scenario: 头文件在 freestanding 构建中可用

- **WHEN** 一个代表性用户程序包含 portable libc subset public headers
- **THEN** 构建 MUST 在 BigOS freestanding 用户程序环境中解析类型、宏和函数声明
- **AND** MUST NOT 通过宿主系统头或隐式 prototype 获取未支持接口

### Requirement: bounded time.h and assert.h headers

BigOS portable libc subset SHALL include bounded `time.h` and `assert.h` public header support. `time.h` MUST expose only the types, constants, and helpers backed by the current BigOS bounded time primitives and MUST NOT claim timezone, locale, calendar conversion, timer, sleep, or complete POSIX time behavior. `assert.h` MUST expose a freestanding-safe `assert` macro contract, MUST support `NDEBUG` disabling, and MUST route assertion failure through a deterministic BigOS user-visible diagnostic or termination path without depending on host libc, dynamic initialization, threads, signals, or complete hosted `abort` semantics.

#### Scenario: time.h exposes bounded time primitive

- **WHEN** 一个简单静态 C 程序包含 `time.h` and uses only documented BigOS portable time declarations
- **THEN** the program MUST compile and link through the BigOS freestanding user libc path
- **AND** the declarations MUST be backed by existing bounded time primitives rather than hosted timezone, locale, or calendar databases

#### Scenario: assert.h supports freestanding assert

- **WHEN** 用户程序包含 `assert.h` and an enabled assertion expression evaluates false
- **THEN** libc MUST follow the documented deterministic diagnostic or termination path
- **AND** the path MUST NOT require host libc, dynamic loader, threads, signals, locale, or complete hosted `abort` behavior

#### Scenario: NDEBUG disables assert evaluation

- **WHEN** 用户程序包含 `assert.h` with `NDEBUG` defined
- **THEN** `assert(expr)` MUST compile as a disabled assertion according to the documented bounded macro contract
- **AND** the disabled assertion MUST NOT evaluate `expr`

### Requirement: ASCII/C-locale ctype 子集

BigOS portable libc subset SHALL provide an ASCII/C-locale-style `ctype` subset for simple programs. The subset MUST define deterministic behavior for common classification and conversion helpers such as alphabetic, digit, space, alphanumeric, printable, upper/lower classification and `toupper`/`tolower` over unsigned char values and EOF-style input. It MUST NOT require locale databases, multibyte encodings, Unicode, wide characters, or host libc behavior.

#### Scenario: ctype 分类确定

- **WHEN** 用户程序对 ASCII 字符、非 ASCII 字节值和 EOF-style 输入调用已支持 `ctype` helper
- **THEN** libc MUST return deterministic results according to the documented portable subset
- **AND** 该行为 MUST NOT depend on locale, host libc, Unicode tables, or dynamic runtime state

#### Scenario: 大小写转换有界

- **WHEN** 用户程序对 ASCII 字母和非字母值调用 `toupper` 或 `tolower`
- **THEN** libc MUST convert supported ASCII letters and leave non-convertible values according to the documented subset
- **AND** MUST NOT require locale-specific case mapping

### Requirement: bounded numeric conversion maturity

BigOS portable libc subset SHALL provide bounded numeric conversion helpers suitable for portable small programs. The subset MUST include unsigned conversion coverage such as `strtoul` and MAY include wider variants when the ABI types are supported. Conversion helpers MUST handle base selection, optional sign where applicable, end pointer reporting, no-digit cases, overflow reporting through documented return and errno behavior, and deterministic parsing of bounded input. They MUST NOT require locale, floating-point conversion, multibyte input, or host runtime support.

#### Scenario: 无符号转换解析成功

- **WHEN** 用户程序调用 supported unsigned conversion helper with a valid bounded decimal, octal, or hexadecimal input
- **THEN** libc MUST return the parsed value
- **AND** MUST set the end pointer according to the first unconsumed character

#### Scenario: 转换失败或溢出可观察

- **WHEN** 用户程序调用 supported numeric conversion helper with no digits or an overflowing bounded input
- **THEN** libc MUST follow the documented return value and errno contract
- **AND** MUST NOT read beyond the provided NUL-terminated input or corrupt caller state

### Requirement: stateless search helper subset

BigOS portable libc subset SHALL include a first batch of stateless search helpers for common string and memory lookup needs. This batch MUST include `strchr`, `strrchr`, `strstr`, and `memchr` with bounded standard-C-style behavior for NUL-terminated strings or caller-provided memory ranges. This batch MUST NOT require hidden global state, tokenizer state, comparator callbacks, sorting stability, input-buffer mutation, locale, host libc, or complete tokenization/search/sort coverage. Stateful tokenization such as `strtok` and comparator-based helpers such as `qsort` and `bsearch` MUST remain outside this required batch unless a later change adds direct consumers and specifications.

#### Scenario: stateless string search succeeds or misses

- **WHEN** 用户程序 calls `strchr`, `strrchr`, or `strstr` on bounded NUL-terminated strings
- **THEN** libc MUST return a pointer to the documented matching location or NULL when no match exists
- **AND** MUST NOT modify the input string or use hidden tokenizer state

#### Scenario: bounded memory search respects range

- **WHEN** 用户程序 calls `memchr` with a pointer and bounded length
- **THEN** libc MUST search only the caller-provided range and return the documented match pointer or NULL
- **AND** MUST NOT read beyond the bounded memory range

#### Scenario: tokenizer and comparator helpers are not implied

- **WHEN** documentation or headers describe the first portable search helper batch
- **THEN** they MUST NOT imply required support for `strtok`, `qsort`, or `bsearch`
- **AND** those helpers MUST require explicit later specification before becoming public supported interfaces

### Requirement: formatter 和错误报告成熟子集

BigOS portable libc subset SHALL provide a shared bounded formatter for stdout, stderr, and bounded string formatting, and MAY route stream output through the bounded buffered `FILE` stream subset defined by the `bounded-file-streams` capability. The formatter MUST preserve existing supported formats and provide deterministic behavior for common integer, unsigned, pointer, size, long, width, truncation, and return-value cases documented by the subset. Error reporting MUST expose deterministic text for supported errno values through bounded helpers such as `strerror` or `perror`. The subset MUST NOT require floating-point formatting, locale, wide characters, complete flags, complete precision, the `scanf` family, or complete hosted stdio.

#### Scenario: formatter 组合行为一致

- **WHEN** 用户程序通过 `printf`、`fprintf(stderr, ...)` 或 `snprintf` 使用 portable subset 支持的格式
- **THEN** libc MUST produce deterministic text through the shared bounded formatter
- **AND** equivalent supported format cases MUST behave consistently across stdout, stderr, and string-buffer outputs

#### Scenario: snprintf 截断返回可观察

- **WHEN** 用户程序调用 `snprintf` with a bounded buffer smaller than the formatted output
- **THEN** libc MUST NUL-terminate when buffer size permits and return the documented would-have-written length or documented bounded equivalent
- **AND** MUST NOT write beyond the caller-provided buffer

#### Scenario: errno 文本可用于错误报告

- **WHEN** 用户程序 reports a supported errno through portable error text helpers
- **THEN** libc MUST produce deterministic user-visible text
- **AND** unsupported or unknown errno values MUST have documented fallback behavior

### Requirement: representative portable program validation

BigOS SHALL validate portable libc subset behavior through representative statically linked user programs and layered checks. Validation MUST cover public header buildability, static linking, `argc`/`argv`/environment access, ctype, numeric conversion, formatter behavior, stdout/stderr, errno translation, error text, file/directory wrapper use, and at least one wrapper failure path. Environment-dependent runtime validation MAY be skipped only with explicit records of missing tools or configuration and remaining risk.

#### Scenario: runtime validation observes portable subset

- **WHEN** portable libc subset runtime validation runs in a configured emulator environment
- **THEN** validation MUST observe at least one representative static user program consuming the portable libc subset
- **AND** results MUST be decidable from user-visible output, exit status, serial/log output, or another deterministic low-level signal

#### Scenario: 环境不可用时记录跳过

- **WHEN** x86_64 cross toolchain, xmake, QEMU, Bochs, display/ROM dependencies, disk image configuration, or timeout oracle are unavailable
- **THEN** corresponding validation MAY be skipped
- **AND** validation notes MUST identify the missing condition, substitute checks that ran, and remaining risk

### Requirement: bounded libc exposes blocking sleep wrappers
BigOS portable libc subset SHALL expose bounded blocking sleep wrappers backed by the BigOS sleep syscall. The wrappers MUST compile and link in the freestanding user program environment, use the repository's syscall/errno translation convention, and avoid implying complete POSIX time, timer, signal-interruptible sleep, or hosted libc behavior.

#### Scenario: BigOS-specific millisecond sleep wrapper
- **WHEN** a user program includes the supported public libc headers and calls `bigos_sleep_ms(milliseconds)`
- **THEN** libc MUST invoke the blocking sleep syscall with the requested millisecond duration
- **AND** it MUST return 0 on success or -1 with `errno` set when the kernel returns a negative errno

#### Scenario: 受限 sleep seconds wrapper
- **WHEN** a user program calls `sleep(seconds)` through the bounded libc subset
- **THEN** libc MUST implement it on top of the BigOS blocking sleep syscall using seconds-to-milliseconds conversion with overflow handling
- **AND** when the underlying syscall fails or a future early-return path becomes observable, libc MUST estimate elapsed whole seconds from monotonic tick readings and return the remaining seconds instead of always returning the original input
- **AND** it MUST document that this wrapper is a bounded POSIX-like compatibility surface, not complete POSIX `sleep(3)` behavior

#### Scenario: signal interruption 语义不被声明
- **WHEN** headers, docs, or examples describe `sleep(seconds)` or `bigos_sleep_ms(milliseconds)`
- **THEN** they MUST NOT claim support for signal interruption, kernel-written remaining time after signal delivery, `nanosleep`, `clock_nanosleep`, `usleep`, `alarm`, timerfd, or high-resolution timers
- **AND** unsupported behavior MUST require a later explicit specification before becoming public supported API

#### Scenario: freestanding header 可构建
- **WHEN** representative static user programs include `unistd.h` or `time.h` and use only the documented bounded sleep declarations
- **THEN** those declarations MUST resolve through BigOS user libc headers without host libc headers
- **AND** the program MUST statically link through the existing user program build path

### Requirement: 缓冲 FILE 流纳入可移植子集

BigOS portable libc subset SHALL include a bounded buffered `FILE` stream subset so that common portable small programs can open named files, perform buffered reads and writes, query stream state, and perform bounded byte positioning without rewriting to raw fd calls. The semantics of this subset MUST be defined by the `bounded-file-streams` capability. This requirement MUST NOT imply complete hosted stdio, the `scanf` family, wide streams, locale, floating-point conversion, temporary files, memory streams, thread-safe stream locking, or complete `fpos_t` positioning.

#### Scenario: 可移植程序经缓冲流读写命名文件

- **WHEN** 一个简单静态 C 程序经 portable subset 的缓冲 `FILE` 流接口 `fopen` 一个命名文件并执行缓冲读写
- **THEN** 该程序 MUST 能通过 BigOS freestanding 静态构建路径解析并链接这些接口
- **AND** 其缓冲、定位与流状态行为 MUST 与 `bounded-file-streams` 能力一致

#### Scenario: 缓冲流子集非目标不被暗示

- **WHEN** 审查 portable subset 的缓冲流 public headers 或文档
- **THEN** 它们 MUST NOT 将 `scanf` 家族、宽流、`tmpfile`/`fmemopen`、locale 或完整 `fpos_t` 定位声明为已支持
- **AND** 这些能力的未来暴露 MUST 伴随显式规格与验证

