## ADDED Requirements

### Requirement: 持久元数据有序同步后跨 clean reboot 可见
BigOS SHALL make successfully synchronized persistent `/rw` metadata updates visible after a clean reboot using the same persistent test disk. The clean-sync guarantee MUST include directory entries, inode metadata, file size, block mappings, and free-space metadata affected by successful create, unlink, rmdir, rename, file growth, truncate, and metadata update operations. This requirement MUST NOT claim journal replay, crash recovery, power-loss safety, automatic repair, or persistence for unsynchronized dirty metadata.

#### Scenario: 同步后的多对象元数据第二次启动可见
- **WHEN** a process creates directories and files under persistent `/rw`, mutates file sizes and directory entries, synchronizes successfully, and validation starts a second emulator run with the same persistent test disk
- **THEN** BigOS MUST remount the persistent `/rw` volume
- **AND** lookup, metadata queries, directory enumeration, and fd I/O MUST observe the synchronized metadata state

#### Scenario: 未同步元数据不扩大承诺
- **WHEN** persistent `/rw` metadata changes exist only as dirty or pending cache state and synchronization has not completed successfully
- **THEN** BigOS MUST NOT claim that those metadata changes survive clean reboot
- **AND** previously synchronized persistent state MUST remain explainable within the clean-sync boundary

### Requirement: 持久卷元数据矛盾不发布可写挂载
BigOS SHALL reject persistent `/rw` mount publication when bounded metadata validation finds incompatible format metadata or internal contradictions. Rejection MUST be deterministic and MUST NOT overwrite the volume, repair the volume, format a replacement volume, alter the read-only exFAT boot asset state, or panic during normal initialization.

#### Scenario: inode 和 free-space 状态冲突
- **WHEN** persistent mount validation finds an inode block mapping that conflicts with free-space metadata
- **THEN** BigOS MUST reject the persistent writable mount or fall back according to the documented policy
- **AND** it MUST NOT expose the inconsistent volume as writable `/rw`

#### Scenario: directory entry 引用非法 inode
- **WHEN** a persistent directory entry references an inode outside the bounded inode table or an inode with invalid type metadata
- **THEN** BigOS MUST reject the persistent writable mount deterministically
- **AND** it MUST NOT publish the invalid directory entry to path lookup
