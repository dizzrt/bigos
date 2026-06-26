## ADDED Requirements

### Requirement: touch 用户工具
BigOS SHALL provide a bounded external `/bin/touch` user tool once file timestamp update syscalls and libc wrappers exist. `touch` MUST create a missing regular file on writable backends or update an existing object's atime and mtime to the current bounded wall-clock time. It MUST report deterministic errors and MUST NOT imply complete POSIX `touch` options, date parsing, timezone conversion, symlink handling, recursive traversal, or nanosecond precision.

#### Scenario: touch 创建缺失文件
- **WHEN** a user runs `touch PATH` for a missing path under a writable supported parent directory
- **THEN** BigOS MUST create an empty regular file
- **AND** stat MUST report initialized atime, mtime, and ctime for that file

#### Scenario: touch 更新已有文件
- **WHEN** a user runs `touch PATH` for an existing supported object with sufficient permission
- **THEN** BigOS MUST update atime and mtime to current bounded wall-clock seconds through the explicit timestamp update interface
- **AND** ctime MUST reflect the timestamp metadata change

#### Scenario: touch 失败可恢复
- **WHEN** `touch` targets a read-only backend, missing parent, unsupported object, invalid path, or path without sufficient permission
- **THEN** it MUST report a deterministic errno-based error and exit nonzero
- **AND** the shell MUST remain usable

### Requirement: stat 工具显示时间戳
BigOS SHALL update the bounded `stat` user tool to print atime, mtime, and ctime fields. The output MUST remain deterministic and numeric, and MUST NOT require locale, timezone, calendar formatting, or hosted libc time conversion.

#### Scenario: stat 输出时间戳字段
- **WHEN** a user runs `stat PATH` for a supported object
- **THEN** the output MUST include atime, mtime, and ctime numeric values
- **AND** unsupported backend timestamps MUST appear as documented bounded defaults
