# stable-file-growth Specification

## Purpose
TBD - created by archiving change add-stable-file-growth. Update Purpose after archive.
## Requirements
### Requirement: 有界常规文件扩展写
BigOS SHALL support bounded extension writes for regular files under `/rw`. A successful write MAY append at EOF, start before EOF and extend beyond EOF, or start after EOF within the configured maximum file size. Reads from any newly created gap between the old EOF and the write start MUST return zero bytes until overwritten. Successful writes MUST update file size, block ownership, dirty cache state, and fd offset consistently. This capability MUST NOT imply complete POSIX sparse files, `pwrite`, writable file-backed `mmap`, async I/O, or unbounded file size.

#### Scenario: 追加写扩展文件大小
- **WHEN** a user process writes bounded data at the current EOF of a `/rw` regular file
- **THEN** BigOS MUST make the written data readable through the same fd, dup-shared fd, inherited fd, and independently reopened path
- **AND** file metadata MUST report the enlarged bounded size

#### Scenario: 越过 EOF 写入形成零读 gap
- **WHEN** a user process seeks beyond EOF and writes bounded data within the file size limit
- **THEN** later reads from the old EOF through the write start MUST return zero bytes
- **AND** later reads from the written range MUST return the written data

#### Scenario: 跨块写入保持顺序内容
- **WHEN** a write spans multiple filesystem blocks and all required blocks are available
- **THEN** BigOS MUST make the complete byte range readable in order
- **AND** it MUST NOT expose stale data from previously freed blocks

### Requirement: 截断收缩和扩展语义
BigOS SHALL support bounded truncate behavior for `/rw` regular files. Shrinking a file MUST publish the new size only after preserving the prefix that remains in range and arranging for blocks beyond the new EOF to become reclaimable. Extending a file through truncate MUST make the new range read as zero bytes until overwritten. Failed truncate operations MUST leave the old file size, contents, block ownership, metadata, and fd offsets explainable from the pre-failure state.

#### Scenario: 收缩截断释放尾部范围
- **WHEN** a user process truncates a `/rw` regular file to a smaller bounded size
- **THEN** reads beyond the new EOF MUST return EOF according to the existing read contract
- **AND** blocks wholly beyond the new EOF MUST become eligible for safe reuse after no live reference owns them

#### Scenario: 扩展截断产生零填充范围
- **WHEN** a user process truncates a `/rw` regular file to a larger bounded size
- **THEN** metadata MUST report the enlarged size
- **AND** reads from the old EOF through the new EOF MUST return zero bytes until overwritten

#### Scenario: 非常规文件截断被拒绝
- **WHEN** a user process attempts to truncate a directory, read-only backend object, missing path, or unsupported object type
- **THEN** BigOS MUST return a deterministic error
- **AND** it MUST NOT modify unrelated directory entries, file data, metadata, or cache state

### Requirement: 稳定块分配和复用
BigOS SHALL maintain stable block ownership for `/rw` file growth and truncate. A data block MUST be owned by at most one live file mapping or by the free-space set, never both. A block released by truncate or unlink MUST be removed from the old inode mapping before reuse. A reused block MUST be zeroed or fully overwritten before user-visible reads can observe it.

#### Scenario: 块复用不泄漏旧内容
- **WHEN** one file releases a data block and another file later receives that block through growth or truncate
- **THEN** reads from the new file MUST return either the new written content or zero-filled bytes for unwritten ranges
- **AND** they MUST NOT expose bytes from the previous owner

#### Scenario: 块所有权不产生别名
- **WHEN** filesystem validation creates multiple growing files and truncates some of them
- **THEN** BigOS MUST keep each live logical file block mapped to a uniquely owned data block or an explicit zero-read range
- **AND** writes to one file MUST NOT modify another file through shared writable block ownership

### Requirement: 文件增长失败保持旧状态
BigOS SHALL make file growth, truncate, and block allocation failures deterministic and state-preserving. If capacity, cache, kernel allocation, user-buffer validation, permission, object type, or block I/O checks fail, BigOS MUST return a deterministic negative errno and MUST NOT publish partial file size, partial block mappings, dirty-cache success, partially initialized data, or unintended fd offset advancement.

#### Scenario: 容量耗尽不发布半成品增长
- **WHEN** a write or extending truncate requires an inode mapping, data block, cache block, or kernel allocation that is unavailable
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** later reads, metadata queries, directory enumeration, and fd offsets MUST observe the pre-failure state except for bytes already committed before the failing operation

#### Scenario: 写回失败不声明持久成功
- **WHEN** file growth or truncate reaches a cache or backing-store write-back failure during synchronization
- **THEN** BigOS MUST return a deterministic error to the caller
- **AND** it MUST NOT mark the affected state as durably committed

### Requirement: 文件增长验证可复现
BigOS SHALL provide default-off validation for stable file growth under the current x86_64 Legacy BIOS emulator path. Validation MUST cover append growth, seek-past-EOF zero reads, cross-block writes, shrink truncate, extend truncate, block reuse, capacity exhaustion, and persistent clean reboot readback when the persistent test disk is available. Missing emulator, toolchain, ROM/display, or disk-image dependencies MUST be recorded as skipped or blocked rather than runtime-passed.

#### Scenario: RAM-backed 增长和截断 smoke
- **WHEN** stable file growth validation is enabled for the RAM-backed `/rw` backend
- **THEN** it MUST exercise bounded growth, gap zero reads, shrink/extend truncate, block reuse, and failure rollback
- **AND** it MUST report deterministic pass, fail, skipped, or blocked results

#### Scenario: persistent clean reboot 读回
- **WHEN** persistent stable file growth validation runs with the required persistent test disk available
- **THEN** the first run MUST grow and truncate files, synchronize successfully, and stop at a clean boundary
- **AND** the second run MUST remount the same persistent `/rw` volume and verify synchronized file sizes, contents, and zero-filled ranges

