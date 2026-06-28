## ADDED Requirements

### Requirement: 有界核心用户态工具集合边界

BigOS SHALL define a bounded core userland utilities set for the default `/bin` userland. The set MUST cover representative file byte-stream tools, directory and path tools, runtime file mutation tools, metadata tools, bounded text filtering tools, time/process helper tools, shell-composition consumers, and BigOS-specific maintenance tools where explicitly documented. This capability MUST NOT claim complete POSIX utilities, GNU coreutils compatibility, complete shell behavior, hosted libc, locale, wide character, Unicode text processing, complete regular expressions, complete sorting, full permissions, symlinks, hard links, async I/O, SMP, broad file-backed `mmap`, or cross-reboot persistence beyond separately specified persistent `/rw` behavior.

#### Scenario: 文档描述 bounded utilities

- **WHEN** BigOS documentation, OpenSpec artifacts, help text, or tool comments describe the default userland tool set
- **THEN** they MUST describe it as a bounded BigOS core utility set
- **AND** they MUST NOT imply complete POSIX utility, GNU coreutils, hosted libc, complete shell, locale, Unicode, regex, sorting, permission, or persistent filesystem compatibility

#### Scenario: 简单用户操作有代表性工具

- **WHEN** a user boots the default BigOS userland and uses `/bin/sh`
- **THEN** the default `/bin` namespace MUST expose representative tools for reading files, listing paths, creating and removing runtime `/rw` entries, querying metadata, processing bounded byte/text streams, observing time or process state, and composing commands through supported shell paths
- **AND** each exposed tool MUST remain within the documented bounded behavior

### Requirement: 工具构建与镜像打包一致

BigOS SHALL build and package supported core userland utilities as freestanding static user ELF programs under the default `/bin` namespace. A utility listed as default-supported MUST have a corresponding build output, MUST be included in the boot image packaging path, MUST remain within the existing user ELF loader size bound, and MUST be executable through the existing `execve` path and shell PATH lookup. This requirement MUST NOT change the x86_64 Legacy BIOS/MBR/exFAT disk layout, user ELF ABI, linker addresses, syscall vector, CR3 switching, or boot handoff contract.

#### Scenario: 默认工具源码、产物和打包清单对齐

- **WHEN** a utility is documented as a default core userland tool
- **THEN** the build and packaging configuration MUST produce a user ELF for that tool and install it at the documented `/bin/<name>` path
- **AND** a default boot image MUST NOT document the tool as available while omitting it from `/bin`

#### Scenario: shell 可执行默认工具

- **WHEN** `/bin/sh` resolves a documented tool name through the default PATH
- **THEN** it MUST be able to launch the packaged utility through the existing `fork`/`execve`/`wait` path
- **AND** the command MUST NOT require dynamic linking, hosted OS services, job control, background execution, or a non-default filesystem layout

#### Scenario: 打包不改变启动和 ABI 关键布局

- **WHEN** the default utility set changes
- **THEN** the existing boot image layout and user ELF loading contract MUST remain compatible with the documented x86_64 Legacy BIOS path
- **AND** any size or image-capacity failure MUST be reported during build or image creation rather than producing a silently missing tool

### Requirement: 工具 I/O、错误报告和退出状态确定

BigOS core userland utilities SHALL provide deterministic input, output, error reporting, and exit status behavior over the bounded fd/VFS/libc subset. Successful commands MUST return zero. Commands that encounter invalid arguments, unsupported options, missing paths, permission/read-only failures, capacity failures, invalid object types, read/write/enumeration errors, or failed child/process operations MUST return non-zero and report an observable diagnostic to stderr when stderr is available. Multi-input utilities MUST preserve successful output for inputs processed before a later failure, while returning non-zero if any required input fails.

#### Scenario: 成功工具返回零

- **WHEN** a supported utility completes its documented operation on valid bounded inputs
- **THEN** it MUST return exit status 0
- **AND** its stdout and filesystem side effects MUST match the documented utility behavior

#### Scenario: 失败工具返回非零并诊断

- **WHEN** a supported utility receives an invalid argument, unsupported option, inaccessible path, invalid object type, read-only target, capacity failure, or I/O failure
- **THEN** it MUST return a non-zero exit status
- **AND** it MUST emit a deterministic stderr diagnostic containing enough context to identify the tool and failing operation or path

#### Scenario: 多输入工具保留已完成输出

- **WHEN** a supported utility processes multiple bounded inputs and one later input fails
- **THEN** output or side effects from earlier successful inputs MUST remain valid
- **AND** the final exit status MUST be non-zero to make the partial failure observable

### Requirement: 字节流和文本过滤工具有界

BigOS SHALL provide bounded byte-stream and text-filtering utilities suitable for small freestanding userland workflows. Supported filters MUST operate on stdin and/or named files through existing fd or bounded `FILE` stream paths, MUST process data with explicit fixed buffers or bounded heap use, and MUST define behavior in byte or ASCII/C-locale terms. They MUST NOT require locale, Unicode collation, wide characters, floating-point formatting, complete regular expressions, complete sorting, unbounded input buffering, memory-mapped files, or host libc behavior.

#### Scenario: 管道输入经文本过滤输出

- **WHEN** a shell pipeline sends bounded text or byte data from one supported command into a filtering utility such as a line, count, substring, dump, or head/tail-style consumer
- **THEN** the filtering utility MUST read from stdin and write deterministic output to stdout through the bounded fd/libc path
- **AND** it MUST NOT require complete POSIX pipe semantics beyond the implemented bounded pipe contract

#### Scenario: 命名文件过滤保持字节语义

- **WHEN** a supported byte/text utility reads one or more named files from `/boot` or `/rw`
- **THEN** it MUST process file bytes according to its documented bounded semantics
- **AND** it MUST NOT depend on locale, Unicode decoding, wide streams, file-backed `mmap`, or host libc

#### Scenario: 未支持 pattern 或 option 确定性失败

- **WHEN** a user invokes a text utility with a pattern form, flag, or option outside the documented bounded subset
- **THEN** the utility MUST reject it deterministically with non-zero exit status and stderr diagnostic
- **AND** it MUST NOT silently claim GNU/POSIX-compatible behavior for that unsupported form

### Requirement: 路径、目录、元数据和文件修改工具复用运行期文件系统语义

BigOS SHALL provide bounded path, directory, metadata, and runtime file mutation utilities that consume the existing VFS, cwd, metadata, directory enumeration, read-only `/boot`, and writable `/rw` contracts. These utilities MUST expose successful `/rw` mutations consistently to later open, stat, list, read, shell, and path-tool observations within the same boot session. They MUST reject unsupported operations or read-only targets through deterministic errors and MUST NOT imply symlink traversal, hard links, full POSIX permissions, atomic replacement, directory rename, journaling, crash recovery, or persistence beyond separately specified `/rw` clean-sync behavior.

#### Scenario: 运行期目录树工具组合一致

- **WHEN** a user uses supported utilities to create a `/rw` directory, write or copy a file into it, list it, stat it, rename or move a regular file, read it back, and remove the entries
- **THEN** each later utility MUST observe the successful state changes from earlier utilities in the same boot session
- **AND** the behavior MUST remain consistent with the runtime filesystem maturity and bounded POSIX-like process/I/O contracts

#### Scenario: 只读启动资产不可被修改

- **WHEN** a file mutation utility targets `/boot` or another read-only path
- **THEN** the utility MUST fail with a deterministic non-zero result and diagnostic
- **AND** it MUST NOT modify read-only boot assets, exFAT state, boot image layout, or unrelated `/rw` entries

#### Scenario: 相对路径工具经 cwd 解析

- **WHEN** a shell or user process changes cwd and invokes supported path utilities with relative paths
- **THEN** those utilities MUST resolve paths through the existing bounded cwd and relative path contract
- **AND** they MUST NOT imply symlink-aware canonicalization, mount namespaces, `chroot`, or complete POSIX pathname behavior

### Requirement: Shell 组合可消费核心工具

BigOS SHALL make the bounded core utility set usable through the existing `/bin/sh` command execution, redirection, and single-stage pipe subset. Utilities MUST compose through inherited fd 0/1/2, supported redirection, and bounded pipe behavior without corrupting shell state. Unsupported shell syntax, unsupported utility options, or failed command setup MUST remain observable and MUST NOT imply complete POSIX shell compatibility.

#### Scenario: redirection 将工具输出写入文件

- **WHEN** `/bin/sh` launches a supported utility with output redirection to a writable `/rw` file
- **THEN** the utility output MUST be written to the redirected file through the existing fd/VFS path
- **AND** the shell MUST remain usable after command completion or deterministic setup failure

#### Scenario: 单级 pipe 连接两个工具

- **WHEN** `/bin/sh` launches a supported single-stage pipeline between two core utilities
- **THEN** bytes written by the upstream utility MUST be available to the downstream utility through the bounded pipe contract
- **AND** the combined behavior MUST be observable through stdout, redirected output, exit status, or deterministic validation logs

#### Scenario: unsupported shell form 不扩大承诺

- **WHEN** a user attempts background jobs, multi-stage shell grammar outside the implemented subset, globbing, variable expansion, command substitution, or scripting control flow
- **THEN** `/bin/sh` or the utility path MUST reject or ignore the unsupported form according to documented bounded shell behavior
- **AND** the rejection MUST NOT be documented as complete shell compatibility

### Requirement: 核心工具验证覆盖构建、打包和运行组合

BigOS SHALL validate bounded core userland utilities through layered checks covering source/build consistency, image packaging, direct utility execution, shell PATH execution, redirection, pipe composition, `/boot` read-only inputs, `/rw` runtime mutations, relative paths, representative text filtering, failure diagnostics, and exit status. Environment-dependent runtime validation MAY be skipped only with explicit records of missing toolchain, emulator, display/ROM dependency, disk image configuration, or timeout oracle and the remaining risk.

#### Scenario: 构建和打包验证发现缺失工具

- **WHEN** validation checks the default core utility set
- **THEN** it MUST detect a documented utility that lacks a build output, exceeds the existing loader bound, or is missing from the packaged `/bin` namespace
- **AND** such a mismatch MUST be treated as validation failure or an explicitly recorded skip with residual risk

#### Scenario: 运行验证观察组合路径

- **WHEN** utility runtime validation runs in a configured emulator environment
- **THEN** it MUST observe representative direct utility execution plus shell PATH, redirection, pipe, `/boot` read, `/rw` mutation, relative path, text filtering, and failure diagnostic scenarios
- **AND** results MUST be decidable from command output, exit status, serial/log output, or another deterministic low-level signal

#### Scenario: 环境不可用时记录跳过

- **WHEN** x86_64 cross toolchain, xmake, QEMU, Bochs, display/ROM dependencies, disk image configuration, or timeout oracle are unavailable
- **THEN** affected runtime validation MAY be skipped
- **AND** validation notes MUST identify the missing condition, substitute checks that ran, and remaining risk
