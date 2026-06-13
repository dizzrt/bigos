## MODIFIED Requirements

### Requirement: 路径元数据查询
BigOS SHALL provide a path-taking metadata query that resolves a bounded NUL-terminated path through the current VFS and returns metadata for the resolved object. Absolute paths MUST resolve from root; relative paths MUST resolve from the current process cwd and support POSIX-style `.`/`..` components within the supported BigOS directory tree. The operation MUST preserve the bounded metadata contract, MUST reject unsupported path forms deterministically, and MUST NOT introduce symbolic-link traversal, mount namespaces, `chroot`, or complete `realpath` semantics.

#### Scenario: 绝对路径查询成功
- **WHEN** a user process queries metadata for an existing absolute path on the read-only exFAT backend or RAM-backed `/rw` backend
- **THEN** BigOS MUST resolve the path through VFS and return metadata matching the resolved object type and size within the bounded metadata contract

#### Scenario: 相对路径查询成功
- **WHEN** 当前进程 cwd 指向支持的目录且用户进程查询一个存在对象的相对路径
- **THEN** BigOS MUST resolve the path from cwd through VFS
- **AND** MUST return metadata matching the cwd-resolved object within the bounded metadata contract

#### Scenario: dot-dot 相对路径查询成功
- **WHEN** 当前进程 cwd 为 `/rw/work/sub` 且用户进程查询 `../note.txt`
- **THEN** BigOS MUST resolve the query as `/rw/work/note.txt`
- **AND** MUST return metadata for that resolved object within the bounded metadata contract

#### Scenario: 缺失路径查询失败
- **WHEN** a user process queries metadata for a missing absolute path or cwd-resolved relative path
- **THEN** BigOS MUST return a deterministic not-found error
- **AND** it MUST NOT publish partially initialized metadata to the user buffer

#### Scenario: 不支持路径形式被拒绝
- **WHEN** a user process queries metadata using an overlong path, non-NUL-terminated path, unsupported relative path form outside the documented `.`/`..` component contract, or otherwise unsupported path form
- **THEN** BigOS MUST fail deterministically
- **AND** it MUST NOT add symlink, mount namespace, `chroot`, or complete `realpath` behavior as a side effect
