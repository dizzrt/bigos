## ADDED Requirements

### Requirement: 有界路径工具集合
BigOS SHALL provide a bounded set of small static userland path tools that make existing path, fd/VFS, cwd, metadata, mkdir, unlink, and file-read contracts observable from the default shell and packaged `/bin` path. The tool set MUST remain single-purpose and bounded; it MUST NOT imply a complete POSIX utility suite, recursive traversal, globbing, scripting, locale-aware formatting, dynamic linking, hosted libc, or complete POSIX shell behavior.

#### Scenario: 工具以静态用户程序打包
- **WHEN** BigOS builds and packages the default userland image
- **THEN** each supported path tool MUST be built as a freestanding static user ELF using the existing user crt0 and libc path
- **AND** each packaged tool MUST be reachable from the existing shell command execution path without changing boot image discovery, MBR, partition, exFAT, linker, or syscall ABI contracts

#### Scenario: 工具范围保持有界
- **WHEN** documentation, help text, specs, or validation describe the path tool set
- **THEN** they MUST describe the tools as a bounded BigOS userland path-tool subset
- **AND** they MUST NOT claim full POSIX utility coverage, full shell grammar, globbing, recursive traversal, symlink traversal, mount namespace behavior, or persistent full writable filesystem semantics

### Requirement: 目录列举工具
BigOS SHALL provide a small directory-listing userland tool that can list entries for a supported directory path through the existing directory enumeration, cwd, and metadata contracts. The tool MUST support absolute paths and cwd-relative paths, MUST report deterministic errors for unsupported inputs, and MUST NOT require POSIX `DIR*`, stable inode identity, locale-aware sorting, recursive traversal, `ls -l` completeness, symlink traversal, or complete permission display.

#### Scenario: 列举 cwd 相对目录
- **WHEN** a user runs the directory-listing tool with a relative directory path from a shell whose cwd points at a supported directory
- **THEN** the tool MUST resolve the path through the libc/kernel cwd contract
- **AND** it MUST print observable entry names for the resolved directory without requiring host OS services or dynamic linking

#### Scenario: 列举缺失目录失败
- **WHEN** a user runs the directory-listing tool for a missing object, regular file where a directory is required, overlong path, or unsupported path form
- **THEN** the tool MUST report a deterministic errno-based error and exit nonzero
- **AND** the shell MUST remain in its bounded read-parse-execute loop after the failure

#### Scenario: 列举不承诺完整 POSIX ls
- **WHEN** the directory-listing output is consumed by validation or documentation
- **THEN** the output MUST be treated as a bounded BigOS directory observation
- **AND** it MUST NOT require stable inode numbers, ACLs, xattrs, complete timestamp formatting, symlink markers, full permission strings, or recursive ordering guarantees

### Requirement: 文件内容查看工具
BigOS SHALL provide or retain a small file-content userland tool that reads one or more supported file paths through libc open/read/close wrappers and writes file bytes to stdout. The tool MUST support absolute and cwd-relative paths, preserve deterministic errno-based failures, and MUST NOT implement complete POSIX `cat` options, terminal control, binary filtering, locale processing, async I/O, or broad device support.

#### Scenario: 查看相对文件内容
- **WHEN** a user runs the file-content tool for a cwd-relative regular file on a supported backend
- **THEN** the tool MUST open the file through the existing libc/kernel path contract and write its contents to stdout
- **AND** the visible output MUST be observable through the shell, console, serial log, or runtime validation path

#### Scenario: 多文件按输入顺序输出
- **WHEN** a user runs the file-content tool with multiple supported file paths
- **THEN** the tool MUST process paths in argv order within bounded argument and file-size behavior
- **AND** a failure for one path MUST be reported deterministically without corrupting unrelated fd state

#### Scenario: 查看目录或缺失文件失败
- **WHEN** a user runs the file-content tool for a missing path, unsupported object type, permission-denied backend path, overlong path, or unsupported path form
- **THEN** the tool MUST report an errno-based error and return a nonzero status
- **AND** it MUST NOT panic the kernel, leak uninitialized memory, or require complete POSIX device semantics

### Requirement: 元数据观察工具
BigOS SHALL provide or retain a small metadata-observation userland tool that displays the bounded file and directory metadata already exposed by the kernel and libc. The tool MUST make object type and size observable when available, MUST report unsupported fields as absent or bounded defaults, and MUST NOT claim complete POSIX `stat`, stable inode identity, ACLs, xattrs, complete timestamps, symlink behavior, or full permission database support.

#### Scenario: 展示文件和目录元数据
- **WHEN** a user runs the metadata-observation tool for an existing regular file or directory
- **THEN** the tool MUST query metadata through the documented libc wrapper and print deterministic bounded fields
- **AND** object type and size MUST be visible when supported by the backend contract

#### Scenario: 元数据错误可观察
- **WHEN** metadata query fails because the path is missing, unsupported, too long, not NUL-terminated at the kernel boundary, or rejected by user-buffer validation
- **THEN** the tool MUST report an errno-based error and exit nonzero
- **AND** failed queries MUST NOT publish partially initialized metadata to user-visible output

### Requirement: 运行时路径变更工具
BigOS SHALL provide bounded userland tools or shell-consumable paths for creating supported directories and removing supported writable runtime paths through existing mkdir/unlink contracts. These tools MUST operate within the RAM-backed `/rw` runtime filesystem boundary where supported, MUST surface read-only backend failures deterministically, and MUST NOT introduce rename, recursive removal, links, symlinks, persistent storage, complete directory removal semantics, or full POSIX permissions.

#### Scenario: 创建 cwd 相对目录
- **WHEN** a user runs the directory-create tool with a cwd-relative path under a supported writable runtime directory
- **THEN** BigOS MUST create the directory through the existing kernel/VFS contract
- **AND** the created directory MUST become observable through directory listing or metadata tools in the same runtime session

#### Scenario: 删除 cwd 相对路径
- **WHEN** a user runs the remove-path tool for a supported cwd-relative writable path
- **THEN** BigOS MUST remove the path according to the existing bounded unlink contract
- **AND** subsequent path metadata or directory listing MUST observe the deletion within the runtime session

#### Scenario: 只读后端变更失败
- **WHEN** a user attempts to create or remove paths on the read-only boot asset backend or an unsupported path form
- **THEN** the tool MUST report a deterministic errno-based failure
- **AND** it MUST NOT mutate boot assets, partition contents, exFAT discovery state, or unrelated `/rw` entries

### Requirement: Shell 消费与组合行为
BigOS shell SHALL allow the supported path tools to run as ordinary external commands through the existing command lookup, `fork`/`execve`/`wait`, fd inheritance, redirection, and pipe subset. Shell consumption MUST preserve relative path behavior, stdout/stderr visibility, and bounded error recovery, and MUST NOT add globbing, quoting expansion, variables, scripting semantics, job control, sessions, terminal process groups, or complete POSIX shell language.

#### Scenario: PATH 查找运行路径工具
- **WHEN** a user enters the name of a packaged path tool without a slash and PATH resolves it under the supported userland path
- **THEN** shell MUST launch the tool through the existing external command execution path
- **AND** tool output and errors MUST remain visible through stdout/stderr according to the bounded fd contract

#### Scenario: 重定向保存工具输出
- **WHEN** a user runs a supported path tool with output redirection to a cwd-relative writable path
- **THEN** shell MUST set up redirection through existing open/dup2 semantics before executing the tool
- **AND** the redirected output MUST be observable through later file-content or metadata tools when the backend supports it

#### Scenario: 管道组合保持有界
- **WHEN** a user composes supported path tools with the existing single-pipe shell subset
- **THEN** shell MUST connect stdout and stdin through the documented pipe/fd behavior
- **AND** unsupported syntax or tool failure MUST report deterministic errors without corrupting the parent shell fd state

### Requirement: 路径工具行为验证
BigOS SHALL provide behavior-oriented validation for the userland path tools. Validation MUST cover successful and failing path tool execution, cwd-relative and absolute paths, read-only versus writable backend differences, metadata visibility, file content visibility, shell command execution, redirection or pipe composition where supported, and explicit skip records for unavailable environment-dependent checks.

#### Scenario: 验证覆盖成功路径
- **WHEN** path-tool validation runs in an environment with the required cross-toolchain, image packaging, and emulator support
- **THEN** it MUST observe at least one directory listing, file content read, metadata display, writable runtime directory creation, writable runtime path deletion, and shell-launched command path
- **AND** each result MUST be decidable through deterministic stdout/stderr, exit status, serial/log output, or another low-level runtime signal

#### Scenario: 验证覆盖失败路径
- **WHEN** path-tool validation exercises unsupported or failing cases
- **THEN** it MUST observe deterministic nonzero failure for missing paths, read-only backend mutation attempts, unsupported path forms, and invalid object type where applicable
- **AND** failures MUST leave the shell and unrelated fd/VFS state usable for subsequent commands

#### Scenario: 环境不可用时记录跳过
- **WHEN** QEMU, Bochs, the `x86_64-elf-*` cross-toolchain, xmake, ROM/display dependencies, disk image configuration, serial oracle, or timeout controls are unavailable
- **THEN** the corresponding runtime validation MAY be skipped with an explicit note of the missing dependency
- **AND** validation notes MUST record substitute checks and remaining risk rather than claiming runtime validation passed
