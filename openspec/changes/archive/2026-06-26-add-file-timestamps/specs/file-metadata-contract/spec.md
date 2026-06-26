## MODIFIED Requirements

### Requirement: 最小元数据结构

BigOS SHALL define a bounded file metadata structure for user-visible `stat`/`fstat` style queries. The structure MUST use fixed-width fields, MUST be fully initialized before copying to user memory, and MUST expose only the bounded subset supported by the current kernel and filesystem backends: object type, size, mode, uid, gid, bounded link count or default value, user-visible object identifier fixed to zero in the first version unless a later object identity capability changes it, and bounded timestamp fields `atime`, `mtime`, and `ctime` expressed as Unix epoch seconds. Unsupported fields MUST remain zero or documented bounded defaults. The structure MUST NOT imply complete POSIX `struct stat` compatibility, device-node semantics, symbolic links, ACLs, extended attributes, nanosecond timestamp precision, timezone conversion, stable inode numbers, or persistent inode identity.

#### Scenario: 常规文件元数据字段有界

- **WHEN** a caller queries metadata for an existing regular file on a supported backend
- **THEN** BigOS MUST return a fully initialized metadata structure with regular-file type, bounded file size, mode, uid and gid values derived from the backend contract or documented defaults
- **AND** timestamp fields MUST be initialized to backend-provided values or documented bounded defaults
- **AND** unsupported fields MUST be zero or documented bounded defaults rather than uninitialized data

#### Scenario: 目录元数据字段有界

- **WHEN** a caller queries metadata for an existing directory on a supported backend
- **THEN** BigOS MUST return directory type and bounded directory metadata, including initialized timestamp fields, without requiring complete directory traversal, `.`/`..`, symbolic links, mount namespaces, or POSIX directory database semantics

#### Scenario: 未支持元数据不扩大兼容承诺

- **WHEN** documentation, headers, or user tools describe the metadata structure
- **THEN** they MUST describe it as a BigOS bounded metadata subset
- **AND** they MUST NOT claim full POSIX `stat` compatibility, full permission database semantics, persistent inode identity, nanosecond timestamp precision, timezone conversion, or complete POSIX timestamp behavior

#### Scenario: 对象编号第一版保持零值

- **WHEN** a caller queries metadata for any supported object on exFAT or `/rw`
- **THEN** BigOS MUST return zero in the user-visible object identifier field unless a later object identity capability explicitly changes that ABI
- **AND** it MUST NOT expose `/rw` runtime inode numbers or exFAT backend identifiers as stable user ABI in this version

## ADDED Requirements

### Requirement: metadata timestamp ABI 镜像一致
BigOS SHALL keep the kernel `bigos::Metadata` timestamp fields and the userland `struct stat` timestamp fields layout-compatible through source-level contract checks. The userland mirror MUST expose `st_atime`, `st_mtime`, and `st_ctime` or documented BigOS-equivalent field names, and libc wrappers MUST copy kernel-returned timestamp values without reinterpretation.

#### Scenario: 内核和用户结构字段一致
- **WHEN** source contract tests inspect metadata structure definitions
- **THEN** they MUST verify that kernel and userland timestamp fields exist in matching order and width
- **AND** failures MUST be reported as source contract violations before runtime image validation
