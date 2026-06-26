## ADDED Requirements

### Requirement: libc 暴露有界时间戳类型与接口
BigOS user libc SHALL expose bounded file timestamp fields and update interfaces for simple static C programs. Public headers MUST define the timestamp fields in `struct stat`, the supported timestamp update wrapper declarations, and any BigOS-specific constants for NOW/OMIT behavior. The headers MUST remain freestanding-safe and MUST NOT depend on hosted libc, dynamic linking, locale, timezone databases, threads, or complete POSIX libc.

#### Scenario: stat 结构包含时间戳字段
- **WHEN** user programs include `sys/stat.h`
- **THEN** `struct stat` MUST expose initialized atime, mtime, and ctime fields matching the kernel metadata ABI
- **AND** unsupported precision or unsupported POSIX fields MUST not be declared as implemented behavior

#### Scenario: timestamp wrapper 可编译
- **WHEN** a small static user C program includes the documented timestamp header and calls the supported wrapper
- **THEN** it MUST compile and link through the existing user crt0/libc build path
- **AND** it MUST not require hosted libc symbols
