## ADDED Requirements

### Requirement: bigfs inode 持久化时间戳
BigOS writable `/rw` backend SHALL store bounded atime, mtime, and ctime values in each supported inode. The inode layout and format version MUST be explicitly updated or proven to have reserved capacity before timestamp fields are published. Persistent test-disk backends with an incompatible old format MUST be rejected or require explicit reformatting rather than silently reinterpreted.

#### Scenario: 新格式 inode 包含时间戳
- **WHEN** bigfs formats a writable backend for this capability
- **THEN** each allocated inode MUST have initialized atime, mtime, and ctime fields
- **AND** stat/fstat on that inode MUST report those fields through the metadata contract

#### Scenario: 旧格式持久化镜像不被误读
- **WHEN** BigOS encounters a persistent bigfs image whose format version or inode layout lacks timestamp fields
- **THEN** it MUST reject the image or require `mkfs_bigfs` reformat before publishing writable `/rw`
- **AND** it MUST NOT reinterpret old inode bytes as valid timestamps

### Requirement: writable operations 更新时间戳
BigOS writable filesystem operations SHALL update inode timestamps according to the file timestamp model. Successful file create, directory create, write, truncate, read, unlink, rmdir, and constrained rename MUST update the affected file or directory timestamps where documented, and failures MUST leave timestamps unchanged except for explicitly documented partial-success cases.

#### Scenario: 创建和 mkdir 初始化时间戳
- **WHEN** `/rw` successfully creates a regular file or directory
- **THEN** the new inode MUST receive atime, mtime, and ctime derived from current wall-clock seconds
- **AND** the parent directory mtime and ctime MUST be updated

#### Scenario: write truncate 更新文件时间
- **WHEN** `/rw` successfully writes to or truncates a regular file
- **THEN** that file's mtime and ctime MUST be updated
- **AND** the resulting timestamp values MUST be observable by later stat/fstat

#### Scenario: read atime 策略有界
- **WHEN** `/rw` successfully reads file data
- **THEN** BigOS MUST apply the documented bounded atime update policy
- **AND** the update MUST remain safe with block-cache dirty tracking and fsync behavior

#### Scenario: 失败路径不更新时间戳
- **WHEN** a writable filesystem operation fails because of permission, capacity, invalid path, unsupported object, invalid buffer, or I/O error
- **THEN** BigOS MUST NOT publish timestamp updates for the failed operation
- **AND** existing data and metadata MUST remain in a consistent pre-failure state
