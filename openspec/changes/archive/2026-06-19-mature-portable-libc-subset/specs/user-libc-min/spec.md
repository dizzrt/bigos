## ADDED Requirements

### Requirement: portable libc subset public header maturity

BigOS 用户态 libc SHALL extend the existing minimal libc public header boundary to support the portable libc subset. Headers MUST expose implemented and specified declarations for common small-program use, including supported standard C subset helpers, bounded `time.h` and `assert.h` declarations, POSIX-like bounded wrappers, BigOS-specific helpers where explicitly opted in, and compatibility exports where needed. Headers MUST NOT expose unsupported hosted libc, complete POSIX libc, dynamic loader, locale, thread, wide-character, complete `FILE` stream, complete timezone/calendar APIs, or unimplemented syscall behavior.

#### Scenario: portable headers expose supported declarations

- **WHEN** 一个简单静态 C 程序包含 portable libc subset headers
- **THEN** it MUST see declarations for implemented supported helpers and wrappers
- **AND** those declarations MUST match the linked BigOS user libc implementation

#### Scenario: unsupported hosted declarations not exposed

- **WHEN** 审查用户态 libc public headers
- **THEN** headers MUST NOT declare unsupported hosted or complete POSIX libc interfaces as available
- **AND** any compatibility export MUST have a documented bounded behavior or a documented migration path

### Requirement: time and assert headers remain bounded

BigOS 用户态 libc SHALL provide `time.h` and `assert.h` as bounded freestanding-safe public headers. `time.h` declarations MUST map to existing BigOS bounded time primitives and MUST NOT expose unsupported timezone, locale, calendar conversion, sleep/timer, or complete POSIX time APIs. `assert.h` MUST provide a deterministic freestanding assertion macro contract with `NDEBUG` support and MUST NOT depend on hosted stderr, signals, dynamic initialization, threads, or complete hosted `abort` semantics.

#### Scenario: time.h 不扩大 time 语义

- **WHEN** 用户程序包含 `time.h`
- **THEN** it MUST see only the types, constants, and helpers backed by BigOS bounded time primitives
- **AND** the header MUST NOT imply timezone, locale, calendar conversion, or complete POSIX time support

#### Scenario: assert.h 断言失败路径确定

- **WHEN** an enabled `assert` expression evaluates false
- **THEN** libc MUST take the documented deterministic diagnostic or termination path
- **AND** the path MUST remain freestanding-safe and independent of hosted libc behavior

### Requirement: ctype and conversion helpers remain freestanding

BigOS 用户态 libc SHALL provide portable subset `ctype` and numeric conversion helpers without host runtime dependencies. `ctype` helpers MUST use deterministic ASCII/C-locale-style behavior. Numeric conversion helpers such as `strtoul` MUST share parsing rules with existing signed conversion behavior where applicable, report end pointer and overflow according to the documented subset, and preserve errno rules for success and failure.

#### Scenario: ctype helper does not depend on locale

- **WHEN** 用户程序调用 supported `ctype` helper
- **THEN** libc MUST compute the result from the input value and documented ASCII/C-locale-style rules
- **AND** MUST NOT consult locale databases, host libc state, or dynamic runtime services

#### Scenario: unsigned conversion follows errno contract

- **WHEN** 用户程序调用 supported unsigned conversion helper with successful input, invalid input, or overflow input
- **THEN** libc MUST return the documented value and update end pointer as specified
- **AND** MUST set errno only when the documented failure or overflow contract requires it

### Requirement: portable formatter and error text

BigOS 用户态 libc SHALL mature the existing bounded stdio/error-reporting subset for portable small programs. `printf`、`fprintf(stderr, ...)`、`snprintf` and related helpers MUST share formatter behavior for supported integer, unsigned, pointer, size, long, width, truncation, and return-value cases. Error text helpers MUST provide deterministic strings for supported errno values and a documented fallback for unknown values. This requirement MUST NOT imply full hosted stdio, floating-point formatting, locale, wide characters, complete flags, complete precision, or complete `FILE` stream behavior.

#### Scenario: shared formatter covers supported cases

- **WHEN** 用户程序 uses a supported portable formatter case through stdout, stderr, or string-buffer output
- **THEN** libc MUST produce deterministic output according to the shared formatter rules
- **AND** the same supported conversion MUST not diverge across output APIs except for the destination

#### Scenario: error text reports supported errno

- **WHEN** 用户程序调用 supported error text helper for a known BigOS errno value
- **THEN** libc MUST return or print a deterministic message for that errno
- **AND** the message path MUST not require locale or hosted stdio

### Requirement: stateless search helpers remain bounded

BigOS 用户态 libc SHALL provide the first portable search helper batch as stateless bounded helpers. The batch MUST include `strchr`, `strrchr`, `strstr`, and `memchr`. These helpers MUST follow documented standard-C-style behavior over NUL-terminated strings or caller-provided memory ranges and MUST NOT modify caller buffers, use hidden tokenizer state, require comparator callbacks, or imply support for `strtok`, `qsort`, or `bsearch`.

#### Scenario: search helper 不修改输入

- **WHEN** 用户程序调用 `strchr`, `strrchr`, `strstr`, or `memchr`
- **THEN** libc MUST return the documented pointer or NULL result
- **AND** MUST NOT mutate the searched string or memory region

#### Scenario: 未纳入 helper 不被声明为已支持

- **WHEN** 审查 public headers for the first portable search helper batch
- **THEN** headers MUST NOT declare `strtok`, `qsort`, or `bsearch` as required supported interfaces
- **AND** any future exposure of those helpers MUST come with explicit specifications and validation

### Requirement: libc wrapper failure semantics remain stable

BigOS 用户态 libc SHALL preserve existing syscall wrapper semantics while exposing a more mature portable subset. Successful wrappers MUST return user-visible success values without clearing or rewriting errno. Failed wrappers MUST translate kernel negative errno to positive user errno and return the documented failure sentinel. libc MUST NOT emulate unsupported kernel behavior in a way that conflicts with the kernel/VFS/process contract.

#### Scenario: success does not rewrite errno

- **WHEN** 用户程序调用一个成功的 libc wrapper after errno already has a value
- **THEN** wrapper MUST return the success result
- **AND** MUST NOT clear or rewrite errno because of success

#### Scenario: failure preserves kernel as source of truth

- **WHEN** kernel returns a negative errno for a supported wrapper
- **THEN** libc MUST translate that value through the shared errno source
- **AND** MUST NOT replace it with an unrelated userland-only error table
