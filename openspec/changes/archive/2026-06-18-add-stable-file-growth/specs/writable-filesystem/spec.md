## ADDED Requirements

### Requirement: `/rw` 扩展写和截断跨操作可见
BigOS SHALL make successful `/rw` regular-file extension writes and truncate operations visible through the existing runtime filesystem consistency surfaces. Successful append writes, seek-past-EOF writes, cross-block writes, shrink truncates, and extend truncates MUST produce consistent current-runtime results across read, stat/fstat, same-fd access, dup-shared fd access, fork/exec-inherited fd access, independent path reopen, and shell-visible bounded tools. The guarantee MUST remain limited to supported writable backends and MUST NOT imply complete POSIX filesystem semantics.

#### Scenario: 扩展写对 fd 和 metadata 一致可见
- **WHEN** a user process extends a `/rw` regular file through append or seek-past-EOF write
- **THEN** later reads through the same fd, dup-shared fd, inherited fd, and independent reopen MUST observe the successful file contents
- **AND** metadata queries MUST report the successful bounded size

#### Scenario: 截断对 fd 和 metadata 一致可见
- **WHEN** a user process successfully shrinks or extends a `/rw` regular file through supported truncate behavior
- **THEN** later reads and metadata queries MUST observe the new size and zero-read behavior for extended ranges
- **AND** dup-related fds MUST preserve shared offset semantics while independent opens use independent offsets

### Requirement: `/rw` 文件增长失败不污染运行期状态
BigOS SHALL preserve explainable runtime filesystem state when `/rw` extension writes, truncate operations, or data-block allocation fail. Permission, object type, path, maximum-size, capacity, cache, kernel allocation, user-buffer, and block I/O failures MUST return deterministic negative errno values and MUST NOT publish partial file data, partial size metadata, dangling block mappings, dirty-cache success, directory entry mutations, or unintended fd offset changes.

#### Scenario: 扩展写容量耗尽保持旧状态
- **WHEN** a `/rw` extension write requires additional data blocks or cache blocks that are unavailable
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** later read, stat/fstat, directory enumeration, and path reopen MUST observe the pre-failure state for the failing operation

#### Scenario: 截断失败不发布部分 size
- **WHEN** a `/rw` truncate operation cannot complete because checks, allocation, cache, or backing I/O fail
- **THEN** BigOS MUST keep the old file size and block ownership explainable
- **AND** it MUST NOT expose a partially truncated or partially extended file

### Requirement: `/rw` 文件增长保持后端隔离
BigOS SHALL keep read-only exFAT boot assets and unsupported backends isolated from `/rw` file growth and truncate operations. Writable growth, truncate, block allocation, dirty-cache state, and free-space metadata updates MUST only target the active writable `/rw` backend after backend mutability and object-type checks pass.

#### Scenario: 只读后端拒绝增长和截断
- **WHEN** a user process attempts extension write or truncate against a read-only exFAT path
- **THEN** BigOS MUST return a deterministic read-only filesystem error
- **AND** it MUST NOT allocate writable blocks, dirty cache state for exFAT metadata, or modify boot assets

#### Scenario: 跨后端状态不互相回滚
- **WHEN** a `/rw` growth or truncate operation fails
- **THEN** BigOS MUST preserve read-only exFAT discovery and future reads
- **AND** it MUST NOT use exFAT state as rollback storage or persistence backing for `/rw`
