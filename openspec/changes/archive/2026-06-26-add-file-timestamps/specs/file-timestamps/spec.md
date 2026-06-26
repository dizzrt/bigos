## ADDED Requirements

### Requirement: BigOS 文件时间戳模型
BigOS SHALL define a bounded file timestamp model with user-visible access time (`atime`), modification time (`mtime`), and metadata change time (`ctime`). Timestamp values MUST be signed or unsigned fixed-width Unix epoch seconds derived from the existing wall-clock path, MUST be fully initialized before publication to user memory, and MUST NOT imply nanosecond precision, timezone formatting, locale formatting, distributed clock synchronization, or complete POSIX timestamp behavior.

#### Scenario: 新建对象初始化时间戳
- **WHEN** BigOS creates a supported regular file or directory on a writable backend
- **THEN** it MUST initialize atime, mtime, and ctime to the current wall-clock Unix seconds or a documented fallback value if wall-clock is degraded
- **AND** metadata queries MUST return initialized timestamp fields rather than reserved or uninitialized data

#### Scenario: 时间戳保持有界语义
- **WHEN** documentation, headers, specs, or user tools describe file timestamps
- **THEN** they MUST describe them as BigOS bounded Unix-second timestamps
- **AND** they MUST NOT claim nanosecond precision, complete POSIX `stat`, timezone conversion, locale formatting, symlink timestamp behavior, or mount timestamp policies

### Requirement: 文件操作更新时间戳
BigOS SHALL update file timestamps for supported writable filesystem operations. Successful read operations SHOULD update atime according to the bounded implementation policy. Successful write and truncate operations MUST update mtime and ctime. Successful metadata-changing operations such as explicit timestamp setting, directory entry creation, directory entry removal, and supported rename MUST update ctime or parent directory mtime/ctime as documented.

#### Scenario: write truncate 更新 mtime ctime
- **WHEN** a user process successfully writes to or truncates a regular file on `/rw`
- **THEN** BigOS MUST update that file's mtime and ctime to the current bounded timestamp value
- **AND** subsequent `stat` or `fstat` MUST observe nondecreasing mtime and ctime

#### Scenario: read 更新 atime
- **WHEN** a user process successfully reads bytes from a regular file on a backend that supports atime mutation
- **THEN** BigOS MUST update or preserve atime according to the documented bounded atime policy
- **AND** it MUST NOT corrupt file data, fd offsets, or dirty-cache state while doing so

#### Scenario: 目录变更更新父目录时间
- **WHEN** a supported create, unlink, rmdir, or rename operation changes a directory's visible entries
- **THEN** BigOS MUST update the affected parent directory mtime and ctime where the backend supports timestamp mutation
- **AND** failures MUST NOT publish partial timestamp updates

### Requirement: 显式更新时间接口
BigOS SHALL provide a bounded user-visible interface to set file atime and mtime by path. The interface MUST validate user pointers, path resolution, permissions, timestamp arguments, and flags before committing changes. A successful explicit timestamp update MUST update ctime to the current wall-clock value. The interface MUST NOT imply complete POSIX `utimensat`, `futimens`, symlink timestamp mutation, nanosecond precision, or directory fd relative path semantics.

#### Scenario: utimens 设置显式时间
- **WHEN** a user process calls the supported timestamp update interface for an existing writable path with valid atime and mtime values
- **THEN** BigOS MUST set the target object's atime and mtime to those values
- **AND** it MUST update ctime to the current bounded timestamp value

#### Scenario: utimens now omit 子集
- **WHEN** a user process requests the documented NOW or OMIT behavior through supported flags
- **THEN** BigOS MUST apply current-time or leave-unchanged behavior only for the supported fields
- **AND** unsupported flags MUST fail deterministically without changing timestamps

#### Scenario: 只读和权限失败不修改时间戳
- **WHEN** timestamp update targets a read-only backend, missing path, unsupported object, invalid user buffer, or path without sufficient permission
- **THEN** BigOS MUST return deterministic errno
- **AND** it MUST NOT mutate atime, mtime, ctime, data blocks, or directory entries

### Requirement: 时间戳用户态工具消费
BigOS SHALL make file timestamps observable through userland tools. The default `stat` tool MUST display atime, mtime, and ctime from bounded metadata. A bounded `touch` tool MUST create a missing file or update an existing file's atime and mtime using the explicit timestamp update interface. These tools MUST NOT claim complete POSIX options, nanosecond precision, date parsing, timezone conversion, symlink handling, or recursive operation.

#### Scenario: stat 显示时间戳
- **WHEN** a user runs `stat PATH` for an object whose metadata query succeeds
- **THEN** the output MUST include atime, mtime, and ctime in deterministic numeric form
- **AND** unsupported backend timestamp values MUST be shown as documented bounded defaults

#### Scenario: touch 创建或更新时间戳
- **WHEN** a user runs `touch PATH`
- **THEN** BigOS MUST create `PATH` if it is missing and the parent is writable, or update atime and mtime to current time if it exists
- **AND** errors MUST be deterministic and leave the shell usable
