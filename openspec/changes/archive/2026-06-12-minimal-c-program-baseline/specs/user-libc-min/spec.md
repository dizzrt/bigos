## ADDED Requirements

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
