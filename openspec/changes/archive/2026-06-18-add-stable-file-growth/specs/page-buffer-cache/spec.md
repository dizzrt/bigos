## ADDED Requirements

### Requirement: 缓存支持文件增长和截断一致性
BigOS SHALL make page/buffer cache state consistent with `/rw` file growth and truncate operations. Newly allocated or zero-read file ranges MUST be represented so later reads return committed data or zero bytes as required. Blocks dirtied by extension writes or truncate metadata updates MUST remain dirty until successfully written back or otherwise synchronized according to the existing cache contract.

#### Scenario: 扩展写后缓存读命中返回新内容
- **WHEN** a `/rw` regular file extension write updates one or more cached blocks and marks them dirty
- **THEN** later reads through the cache MUST return the written content for the committed range
- **AND** unwritten gap ranges MUST return zero bytes

#### Scenario: 截断后缓存不返回旧尾部数据
- **WHEN** a `/rw` regular file is successfully truncated to a smaller size
- **THEN** cache reads beyond the new EOF MUST NOT return stale tail data from blocks formerly owned by that file
- **AND** dirty or clean cached blocks that no longer belong to the file MUST be invalidated, remapped, or made unreachable from that file

### Requirement: 缓存写回失败不发布文件增长 durable success
BigOS SHALL keep cache write-back failure behavior explicit for file growth and truncate. If write-back of data blocks, inode metadata, free-space metadata, or directory-relevant metadata fails, the cache and filesystem MUST return a deterministic error and MUST NOT mark the affected growth or truncate state as durably committed.

#### Scenario: fsync 增长文件写回失败
- **WHEN** `fsync` attempts to write dirty blocks for a grown `/rw` regular file and the backing store reports an error
- **THEN** BigOS MUST return a deterministic write-back error
- **AND** it MUST preserve dirty or pending-write state instead of silently discarding the update

#### Scenario: 淘汰截断 metadata 失败
- **WHEN** cache eviction attempts to write metadata required for a truncate operation and the write-back fails
- **THEN** BigOS MUST keep the affected state explainable and report failure through the synchronization path
- **AND** it MUST NOT reuse the cache slot as though the metadata were durably written

### Requirement: 缓存块复用不泄漏释放文件数据
BigOS SHALL prevent stale data exposure when cache slots or backing blocks released by truncate or unlink are reused for file growth. Before a reused block becomes user-visible through another file or an extended range, BigOS MUST zero the visible unwritten bytes or fully overwrite them with committed data.

#### Scenario: 释放块被新文件复用
- **WHEN** a block formerly owned by one file is freed and later reused for another `/rw` file
- **THEN** cache-backed reads from the new file MUST NOT expose the old file's bytes
- **AND** the new file MUST observe either committed writes or zero-filled unwritten ranges

#### Scenario: cache slot 复用后键和内容匹配
- **WHEN** a cache slot previously associated with one file data block is reused for another device/block key
- **THEN** the cache MUST associate the slot with the new key and valid content before returning it to readers
- **AND** it MUST NOT return stale content under the new key
