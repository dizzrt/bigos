## ADDED Requirements

### Requirement: 默认镜像打包日常用户工具
BigOS SHALL package a bounded daily-interaction userland tool set in the default user image. The tool set MUST be built as freestanding static user ELF programs using the existing user crt0/libc path and MUST remain reachable through `/bin` and shell PATH lookup. This capability MUST NOT change boot image discovery, disk layout, linker addresses, IDT/syscall vectors, page-table layout, CR3 switching, or existing syscall numbering.

#### Scenario: 默认构建生成工具
- **WHEN** the default BigOS build packages userland artifacts
- **THEN** `/bin/sh` and each supported daily external tool MUST be built under the existing user program build path
- **AND** each tool MUST remain under the existing bounded user ELF size limit

#### Scenario: PATH 可运行工具
- **WHEN** a user enters a supported external daily tool name in `/bin/sh` without a slash
- **THEN** shell PATH lookup MUST be able to resolve and execute the packaged `/bin/<tool>` program
- **AND** the parent shell MUST remain usable after the tool exits

### Requirement: 日常工具边界保持有界
BigOS daily tools SHALL expose practical interactive behavior without claiming complete POSIX utility semantics. Each tool MUST report deterministic failures through stderr/stdout and nonzero exit status. The tool set MUST NOT imply hosted libc, dynamic linking, regex engines, locale-aware formatting, shell globbing, symlink traversal, mount namespace behavior, complete termios, or complete POSIX tool options.

#### Scenario: 工具失败可恢复
- **WHEN** a supported daily tool receives a missing path, invalid argument, unsupported object type, overlong path, read-only mutation target, or unsupported capability
- **THEN** it MUST report a deterministic bounded error and exit nonzero
- **AND** shell MUST remain able to run a subsequent command

#### Scenario: 工具描述不扩大承诺
- **WHEN** help text, specs, validation notes, or source comments describe the daily tools
- **THEN** they MUST describe the tools as BigOS bounded userland utilities
- **AND** they MUST NOT claim complete POSIX utility coverage

### Requirement: 文件时间戳 touch 不属于本能力
BigOS SHALL NOT claim complete `touch` timestamp update behavior as part of this daily tool change. Timestamp mutation requires a separate metadata and syscall contract because current user-visible metadata does not expose atime, mtime, or ctime fields.

#### Scenario: 当前变更不发布 touch 时间戳能力
- **WHEN** this change is implemented and validated
- **THEN** it MUST NOT claim that BigOS supports POSIX `touch` timestamp update behavior
- **AND** any future timestamp support MUST be specified as a separate filesystem metadata capability
