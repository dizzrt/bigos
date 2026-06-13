## ADDED Requirements

### Requirement: libc 暴露 cwd 与相对路径 wrapper
BigOS 用户态 libc SHALL expose bounded current-directory wrappers and path-taking wrappers for simple static C programs. At minimum, libc MUST provide declarations and implementations for `chdir` and `getcwd`, expose `ERANGE` for cwd buffer-size failures, and let existing path-taking wrappers pass relative paths to the kernel unchanged so the kernel/VFS cwd contract remains authoritative. Wrappers MUST follow the existing syscall ABI, translate kernel negative errno returns into positive user `errno` plus documented failure sentinels, and MUST NOT claim complete POSIX libc support.

#### Scenario: chdir wrapper 成功
- **WHEN** 用户程序调用 `chdir` 指向一个存在目录且内核返回成功
- **THEN** libc MUST return the documented success value
- **AND** subsequent relative path wrappers in the same process MUST observe the new cwd through kernel path resolution

#### Scenario: chdir wrapper 失败设置 errno
- **WHEN** 内核因缺失路径、非目录、无效用户路径或不支持路径形式拒绝 `chdir`
- **THEN** libc MUST set `errno` to the corresponding positive value
- **AND** MUST return the documented failure sentinel without changing userland shadow state

#### Scenario: getcwd wrapper 返回当前路径
- **WHEN** 用户程序以足够大的缓冲区调用 `getcwd`
- **THEN** libc MUST invoke the kernel cwd query through the documented syscall ABI
- **AND** MUST return a user-visible pointer or success result consistent with the wrapper contract

#### Scenario: getcwd wrapper 小缓冲设置 ERANGE
- **WHEN** 内核因用户缓冲区容量不足返回 `-ERANGE`
- **THEN** libc MUST set `errno` to `ERANGE`
- **AND** MUST return the documented failure sentinel without modifying caller-visible cwd state

#### Scenario: path wrapper 不自行 canonicalize
- **WHEN** 用户程序通过 libc 调用 `open`、`stat`、`execve` 或其他 path-taking wrapper with a relative path containing ordinary components, `.`, or `..`
- **THEN** libc MUST pass the bounded user path to the kernel without implementing its own namespace, symlink, `chroot`, or full `realpath` behavior
- **AND** kernel negative errno MUST remain the single source for failure translation

### Requirement: cwd 头文件边界
BigOS 用户态 libc SHALL expose cwd-related declarations only in freestanding-safe headers that already represent the bounded userland ABI. Headers MUST include only implemented constants, types, and prototypes needed for cwd and path wrappers, and MUST NOT imply hosted filesystem APIs, thread-safe cwd databases, dynamic loader behavior, or complete POSIX path support.

#### Scenario: 头文件可供简单 C 程序包含
- **WHEN** 简单静态 C 程序包含 BigOS userland headers for path operations
- **THEN** the program MUST see declarations for supported cwd wrappers and path-taking wrappers
- **AND** those declarations MUST NOT depend on host libc, OS services, threads, shared libraries, or dynamic initialization

#### Scenario: 未实现接口不被声明为支持
- **WHEN** 文档或 headers 描述 cwd/path support
- **THEN** they MUST describe it as a bounded BigOS subset with POSIX-style `.`/`..` component handling
- **AND** MUST NOT claim full POSIX `realpath`, `fchdir`, `openat`, symlink, mount namespace, or thread-local cwd semantics unless a later spec adds them
