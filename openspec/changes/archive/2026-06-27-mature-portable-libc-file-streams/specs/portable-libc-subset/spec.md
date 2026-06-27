## MODIFIED Requirements

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

## ADDED Requirements

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
