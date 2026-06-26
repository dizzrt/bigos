## ADDED Requirements

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
