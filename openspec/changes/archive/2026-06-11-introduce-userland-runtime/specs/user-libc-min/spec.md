## ADDED Requirements

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
