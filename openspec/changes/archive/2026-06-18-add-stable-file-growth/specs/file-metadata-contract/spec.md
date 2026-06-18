## ADDED Requirements

### Requirement: 文件增长和截断更新有界 size metadata
BigOS SHALL make bounded path and fd metadata queries reflect successful `/rw` regular-file extension writes and truncate operations. Metadata MUST report the committed file size after append writes, seek-past-EOF writes, cross-block writes, shrink truncates, and extend truncates on supported writable backends. Failed growth or truncate operations MUST leave metadata explainable from the pre-failure state.

#### Scenario: 扩展写后 stat 报告新大小
- **WHEN** a user process successfully extends a `/rw` regular file and then queries metadata by path or valid fd
- **THEN** BigOS MUST report the enlarged bounded file size
- **AND** the reported object type, owner, mode, uid, and gid fields MUST remain consistent with the existing metadata subset

#### Scenario: 截断后 stat 报告提交大小
- **WHEN** a user process successfully shrinks or extends a `/rw` regular file through supported truncate behavior and then queries metadata by path or fd
- **THEN** BigOS MUST report the committed truncated size
- **AND** it MUST NOT report an intermediate size from a failed operation

### Requirement: metadata 查询不暴露块分配细节
BigOS SHALL keep file metadata bounded while file growth and truncate mature. Metadata MAY expose supported file type, size, mode, uid, gid, and documented bounded defaults, but MUST NOT expose raw data block numbers, free-space metadata, stable inode identity beyond existing contracts, allocation generation counters, sparse extent details, journaling state, or crash-recovery status.

#### Scenario: 查询增长文件不返回块布局
- **WHEN** a user process queries metadata for a grown or truncated `/rw` regular file
- **THEN** BigOS MUST return only the supported bounded metadata fields
- **AND** it MUST NOT expose raw block allocation layout or internal free-space state

#### Scenario: 失败增长后 metadata 保持旧状态
- **WHEN** a file growth or truncate attempt fails before publishing the new file state
- **THEN** subsequent metadata queries MUST report the last successfully committed size and supported attributes
- **AND** they MUST NOT reveal partially prepared block allocation state
