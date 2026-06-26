## Purpose

定义 BigOS 有界用户态路径工具集合，使目录列举、文件内容查看、元数据观察、目录创建、路径删除、受限 rename 和 shell 组合行为可通过现有 cwd、fd/VFS、metadata、libc、用户程序构建和运行时验证契约观察。
## Requirements
### Requirement: 有界路径工具集合
BigOS SHALL provide a bounded set of small static userland path tools that make existing path, fd/VFS, cwd, metadata, mkdir, unlink, constrained rename, and file-read contracts observable from the default shell and packaged `/bin` path. The tool set MUST remain single-purpose and bounded; it MUST NOT imply a complete POSIX utility suite, recursive traversal, globbing, scripting, locale-aware formatting, dynamic linking, hosted libc, or complete POSIX shell behavior.

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
BigOS SHALL provide bounded userland tools or shell-consumable paths for creating supported directories, removing supported writable runtime paths, and renaming supported regular files through existing mkdir/unlink/rename contracts. These tools MUST operate within the RAM-backed `/rw` runtime filesystem boundary where supported, MUST surface read-only backend failures deterministically, and MUST NOT introduce recursive removal, links, symlinks, persistent storage, complete directory removal semantics, complete POSIX `mv`, or full POSIX permissions.

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

### Requirement: 用户态 rename 工具

BigOS SHALL provide a small userland path tool or equivalent shell-consumable user program that invokes the bounded libc rename wrapper and makes `/rw` file rename behavior observable from the default shell and packaged `/bin` path. The tool MUST support absolute paths and cwd-relative paths through the existing libc/kernel path contract, MUST report deterministic errno-based failures, and MUST NOT imply complete POSIX `mv`, recursive moves, globbing, interactive prompting, cross-device moves, symlink behavior, dynamic linking, hosted libc, or complete POSIX shell behavior.

#### Scenario: shell 中重命名 cwd 相对文件

- **WHEN** 用户在 cwd 位于 `/rw/work` 的 shell 中运行 rename 工具，把 `a.txt` 改名为 `b.txt`
- **THEN** 工具 MUST 通过 libc rename wrapper 发起操作并在成功时退出为成功状态
- **AND** 后续目录列举、文件内容查看或元数据工具 MUST 能观察到目标名称，且源名称不再可见

#### Scenario: 工具报告只读和跨后端失败

- **WHEN** 用户尝试通过 rename 工具修改只读 boot asset、跨后端移动路径、重命名缺失路径或覆盖已存在目标
- **THEN** 工具 MUST 报告 errno-based 错误并返回非零状态
- **AND** shell MUST 保持在有界 read-parse-execute 循环中，后续命令仍可运行

#### Scenario: 工具范围不承诺完整 POSIX mv

- **WHEN** documentation、help text、specs 或 validation 描述 rename 工具
- **THEN** 它们 MUST 将该工具描述为 BigOS 有界 rename 消费路径
- **AND** MUST NOT 声称支持完整 POSIX `mv`、目录树搬移、目标替换、跨设备复制回退、交互确认、备份、glob 或 locale-aware 输出

### Requirement: rename 行为验证可观察

BigOS SHALL provide behavior-oriented validation for the constrained rename capability through userland-visible behavior. Validation MUST cover successful `/rw` rename, cwd-relative rename, source disappearance, target content preservation, already-open fd behavior where practical, and deterministic failures for read-only backend, missing source, existing target, unsupported object type and unavailable environment-dependent checks.

#### Scenario: 验证覆盖成功 rename

- **WHEN** rename validation runs in an environment with required cross-toolchain, image packaging, shell/userland and emulator support
- **THEN** it MUST observe at least one `/rw` file renamed through the userland path
- **AND** the result MUST be decidable through deterministic stdout/stderr, exit status, serial/log output or another low-level runtime signal

#### Scenario: 验证覆盖失败路径

- **WHEN** rename validation exercises unsupported or failing cases
- **THEN** it MUST observe deterministic nonzero failure for missing paths, read-only backend mutation attempts, target-exists behavior, unsupported object type or cross-backend requests where applicable
- **AND** failures MUST leave the shell and unrelated fd/VFS state usable for subsequent commands

#### Scenario: 环境不可用时记录跳过

- **WHEN** QEMU, Bochs, the `x86_64-elf-*` cross-toolchain, xmake, ROM/display dependencies, disk image configuration, serial oracle or timeout controls are unavailable
- **THEN** corresponding runtime validation MAY be skipped with an explicit note of the missing dependency
- **AND** validation notes MUST record substitute checks and remaining risk rather than claiming runtime validation passed

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

### Requirement: 路径工具在 shell 组合中保持确定性行为

BigOS userland path tools SHALL remain deterministic when consumed by the bounded shell through PATH lookup, cwd-relative paths, stdout redirection, stdin redirection where applicable, and single-pipe composition. Each tool MUST report supported failures through errno-based output or deterministic tool errors, MUST return bounded nonzero status on failure, and MUST NOT require complete POSIX utility options, recursive traversal, globbing, scripting, locale-aware formatting, dynamic linking, hosted libc, or complete POSIX shell behavior.

#### Scenario: PATH 查找运行工具并传播状态

- **WHEN** 用户在 shell 中通过 PATH 运行一个 packaged path tool
- **THEN** shell MUST launch it through the existing external command path
- **AND** the tool MUST return a bounded status that lets shell and validation distinguish success from deterministic failure

#### Scenario: 工具错误不破坏 shell 循环

- **WHEN** a path tool fails because of missing path, unsupported object type, read-only backend mutation, path length, permission, or capacity boundary
- **THEN** the tool MUST report a deterministic errno-based or bounded tool error and exit nonzero
- **AND** shell MUST remain able to run a subsequent command

#### Scenario: 工具组合不扩大工具集承诺

- **WHEN** documentation, help text, specs, or validation describe path tools used with pipe or redirection
- **THEN** they MUST describe the behavior as BigOS bounded path-tool composition
- **AND** they MUST NOT claim complete POSIX `ls`, `cat`, `stat`, `mkdir`, `rm`, `mv`, shell language, globbing, recursive traversal, symlink traversal, or persistent filesystem behavior

### Requirement: 路径工具输出可经 redirection 与 pipe 观察

BigOS path tools SHALL provide stdout/stderr behavior that composes with the bounded shell's existing fd, redirection, and pipe contracts. Tool output redirected to writable runtime paths MUST be observable by later supported path tools when the backend supports it; pipe output MUST be consumable by a supported downstream command within bounded pipe limits.

#### Scenario: 重定向保存路径工具输出

- **WHEN** a user redirects a supported path tool's stdout to a cwd-relative file under a writable runtime directory
- **THEN** the shell MUST route stdout through existing open/dup2 semantics
- **AND** a later supported file-content or metadata tool MUST be able to observe the redirected file if the write succeeded

#### Scenario: pipe 传递路径工具输出

- **WHEN** a user connects a supported path tool's stdout to another supported command through a single pipe
- **THEN** data written by the upstream tool MUST be available to the downstream command according to bounded pipe semantics
- **AND** unsupported syntax, upstream failure, downstream failure, or pipe capacity limits MUST remain deterministic and recoverable

### Requirement: 日常文件复制与移动工具
BigOS SHALL provide bounded external tools for `cp` and `mv`. `cp` MUST copy bytes from one supported regular file path to another through existing fd/VFS read/write paths. `mv` MUST invoke the existing bounded rename contract. These tools MUST support absolute and cwd-relative paths and MUST NOT imply recursive copy, directory tree moves, symlink behavior, cross-device copy fallback, interactive prompts, backups, or complete POSIX options.

#### Scenario: cp 复制文件内容
- **WHEN** 用户运行 `cp SRC DST` and both paths are supported regular file paths
- **THEN** BigOS MUST create or truncate `DST` and write bytes read from `SRC`
- **AND** later file-content tools MUST observe copied content when the backend supports writes

#### Scenario: mv 使用 rename 契约
- **WHEN** 用户运行 `mv OLD NEW` on paths supported by the bounded rename contract
- **THEN** BigOS MUST rename `OLD` to `NEW` through libc `rename`
- **AND** unsupported cross-backend, missing source, existing target, or read-only cases MUST fail deterministically

### Requirement: 日常写入与追加工具
BigOS SHALL provide bounded external `tee`, `write`, and `append` tools. `tee` MUST copy stdin to stdout and one or more files. `write` MUST write argv text to a target file. `append` MUST append argv text to a target file by seeking to end before writing. These tools MUST NOT claim atomic append, shell quoting, here-documents, or complete POSIX option behavior.

#### Scenario: tee 保存并透传 stdin
- **WHEN** 用户通过 pipe or redirection sends bytes to `tee PATH`
- **THEN** `tee` MUST write those bytes to stdout and to `PATH`
- **AND** write failures MUST produce deterministic nonzero status

#### Scenario: write append 写入参数文本
- **WHEN** 用户运行 `write PATH TEXT...` or `append PATH TEXT...`
- **THEN** the selected tool MUST write the argument text separated by spaces and followed by newline
- **AND** `append` MUST preserve existing content before the appended text when the backend supports seek and writes

### Requirement: 日常内容查看与筛选工具
BigOS SHALL provide bounded external `head`, `tail`, `wc`, `grep`, and `hexdump` tools. `head` and `tail` MUST show bounded portions of input, `wc` MUST count lines, words, and bytes, `grep` MUST support plain substring matching only, and `hexdump` MUST print deterministic hexadecimal bytes. These tools MUST work with files or stdin where practical and MUST NOT imply regex, locale, binary classification, unlimited buffering, or complete POSIX options.

#### Scenario: head tail 查看部分内容
- **WHEN** 用户运行 `head PATH` or `tail PATH`
- **THEN** the tool MUST print a bounded default number of lines from the start or end of the file
- **AND** invalid paths or unsupported input MUST fail deterministically

#### Scenario: grep 普通子串匹配
- **WHEN** 用户运行 `grep NEEDLE PATH` or pipes input into `grep NEEDLE`
- **THEN** `grep` MUST print input lines containing `NEEDLE` as a plain substring
- **AND** regex metacharacters MUST be treated as ordinary bytes

#### Scenario: hexdump 打印十六进制
- **WHEN** 用户运行 `hexdump PATH` or pipes input into `hexdump`
- **THEN** the tool MUST print deterministic offsets, hex bytes, and printable ASCII fallback within bounded line widths

### Requirement: 日常系统观察与控制工具
BigOS SHALL provide bounded external `date`, `kill`, and `sleep` tools using existing wall-clock, signal, and blocking sleep libc wrappers. These tools MUST NOT imply time zones, locale formatting, complete signal name databases, signal-interruptible sleep, nanosleep, alarm, timerfd, or high-resolution timers.

#### Scenario: date 输出当前秒级时间
- **WHEN** 用户运行 `date`
- **THEN** the tool MUST read the current wall-clock Unix seconds through the existing libc time path and print a deterministic numeric representation

#### Scenario: kill 发送信号
- **WHEN** 用户运行 `kill PID [SIGNO]`
- **THEN** the tool MUST invoke libc `kill` with default `SIGTERM` when no signal is provided
- **AND** permission, missing PID, or invalid signal failures MUST be reported deterministically

#### Scenario: sleep 阻塞等待
- **WHEN** 用户运行 `sleep SECONDS`
- **THEN** the tool MUST call the existing blocking libc `sleep`
- **AND** it MUST NOT busy-wait as a substitute for scheduler sleep

### Requirement: 日常路径名与树观察工具
BigOS SHALL provide bounded external `basename`, `dirname`, `more`, `find`, and `du` tools. `basename` and `dirname` MUST operate on path strings. `more` MUST page text through the default terminal using BigOS terminal mode when interactive. `find` MUST traverse supported directory trees within fixed path and recursion bounds. `du` MUST report metadata size totals, not physical block usage. These tools MUST NOT imply symlink traversal, mount namespaces, complete terminal UI, or POSIX option compatibility.

#### Scenario: basename dirname 处理路径字符串
- **WHEN** 用户运行 `basename PATH` or `dirname PATH`
- **THEN** the tool MUST print the final path component or parent path using bounded string handling

#### Scenario: more 分页显示文本
- **WHEN** 用户运行 `more PATH` on the default interactive terminal
- **THEN** the tool MUST display text page by page and accept bounded raw-mode key input to continue or quit
- **AND** it MUST restore canonical mode before normal exit

#### Scenario: find du 遍历目录树
- **WHEN** 用户运行 `find PATH` or `du PATH`
- **THEN** the tool MUST traverse supported directory entries within fixed recursion and path-length limits
- **AND** failures for overlong paths, unsupported objects, or read errors MUST be deterministic and recoverable

