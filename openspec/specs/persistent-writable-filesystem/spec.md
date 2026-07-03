# persistent-writable-filesystem Specification

## Purpose
TBD - created by archiving change add-persistent-writable-filesystem. Update Purpose after archive.
## Requirements
### Requirement: 持久可写卷识别与挂载
BigOS SHALL provide a bounded persistent writable filesystem volume for the current x86_64 Legacy BIOS storage path using a build-tool-created independent persistent test disk. The persistent volume MUST be identified by explicit on-disk metadata including magic, current format version, block size, capacity limits, and root metadata, and MUST be mounted as the writable `/rw` backend only after all recognition checks pass. This capability MUST NOT modify the MBR, exFAT boot assets, packaged user binaries, boot handoff ABI, page-table layout, interrupt vectors, or syscall ABI.

#### Scenario: 识别既有持久卷并挂载为 `/rw`
- **WHEN** the kernel initializes filesystem support and finds a compatible persistent writable volume on the independent persistent test disk
- **THEN** BigOS MUST mount that volume as `/rw`
- **AND** existing read-only exFAT boot assets MUST remain readable and isolated from `/rw` mutations

#### Scenario: 不兼容卷拒绝挂载
- **WHEN** the independent persistent test disk has an invalid magic, old version, unknown version, future version, invalid block size, invalid capacity bounds, or inconsistent root metadata
- **THEN** BigOS MUST reject the persistent mount with a deterministic diagnostic or error path
- **AND** it MUST NOT overwrite the volume, auto-migrate the volume, auto-format the volume, modify read-only exFAT state, or panic during normal initialization

### Requirement: 显式格式化新持久卷
BigOS SHALL support an explicit controlled formatting path for an empty persistent writable volume through a minimal user-space mkfs tool. The mkfs tool MUST remain bounded and MUST NOT imply a complete POSIX mount/format toolchain. Formatting MUST initialize the superblock, free-space metadata, root directory, inode metadata, and required data blocks before publishing the volume as `/rw`. Normal boot MUST NOT silently format a persistent area merely because recognition failed.

#### Scenario: 显式格式化后发布挂载
- **WHEN** the minimal user-space mkfs tool or default-off validation path requests formatting of an empty supported independent persistent test disk
- **THEN** BigOS MUST initialize a complete bounded persistent filesystem image
- **AND** it MUST publish `/rw` only after all required metadata writes and verification reads succeed

#### Scenario: mkfs 工具保持有界
- **WHEN** the minimal mkfs tool is used to format the persistent test disk
- **THEN** it MUST only expose the bounded formatting behavior needed by BigOS persistent `/rw`
- **AND** it MUST NOT claim complete POSIX `mkfs`, device management, partition editing, or mount tooling compatibility

#### Scenario: 普通挂载失败不自动格式化
- **WHEN** normal boot attempts to mount a persistent volume and recognition fails
- **THEN** BigOS MUST NOT automatically format the area
- **AND** it MUST either fall back to the RAM-backed `/rw` mode or leave persistent `/rw` unpublished according to the configured policy

### Requirement: 同步后的状态跨 clean reboot 可见
BigOS SHALL make successfully synchronized persistent `/rw` file data, directory entries, and bounded metadata visible after a clean reboot using the same independent persistent test disk image. The persistence guarantee MUST apply to states committed by successful `fsync`, explicit sync, or cache write-back before the clean shutdown or validation reboot boundary. This requirement MUST NOT claim crash consistency, journal replay, power-loss safety, or recovery from partially completed metadata updates after an unclean stop.

#### Scenario: 写入同步后第二次启动读回
- **WHEN** a user process creates a file under persistent `/rw`, writes bounded content, calls `fsync` successfully, and the validation script starts the emulator a second time with the same independent persistent test disk image
- **THEN** BigOS MUST remount the persistent `/rw` volume
- **AND** reopening the file MUST return the synchronized content and bounded metadata

#### Scenario: 未同步 dirty 数据不扩大承诺
- **WHEN** a user process writes data under persistent `/rw` but the dirty blocks are not successfully synchronized before reboot
- **THEN** BigOS MUST NOT claim that the unsynchronized data persists
- **AND** previously synchronized filesystem state MUST remain explainable within the non-journaled clean-sync boundary

### Requirement: 持久后端失败保持可解释状态
BigOS SHALL return deterministic negative errno values or documented diagnostics for persistent `/rw` failures including capacity exhaustion, allocation failure, block I/O failure, incompatible volume metadata, unsupported object types, invalid paths, permission denial, and write-back failure. Failed operations MUST NOT publish partially initialized files, directories, rename targets, truncated metadata, dirty-cache success, or corrupted read-only exFAT state.

#### Scenario: 容量耗尽不发布半成品
- **WHEN** persistent `/rw` create, write, mkdir, rename, truncate, or metadata update requires an inode, directory slot, data block, cache block, or kernel allocation that is unavailable
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** later lookup, read, metadata query, and directory enumeration MUST observe the pre-failure state

#### Scenario: 块写失败不静默成功
- **WHEN** persistent `/rw` `fsync`, sync, or cache eviction cannot write required blocks to the underlying block device
- **THEN** BigOS MUST report a deterministic write-back error
- **AND** it MUST NOT silently discard dirty data or mark the affected persistent state as successfully committed

### Requirement: 持久文件系统验证可复现
BigOS SHALL provide default-off validation that exercises persistent writable filesystem behavior under the current x86_64 Legacy BIOS emulator path. Validation MUST record toolchain, emulator, ROM/display, and disk-image availability; passed checks; skipped checks with reasons; and residual risk.

#### Scenario: 双启动 smoke validates persistence
- **WHEN** persistent filesystem validation is enabled and the required xmake, x86_64-elf toolchain, emulator, and writable disk image are available
- **THEN** validation MUST use one script to launch the emulator twice with the same independent persistent test disk image: the first launch formats or mounts the volume, creates and synchronizes bounded files and directories, and emits a write-stage result; the second launch remounts `/rw`, verifies the synchronized state, and emits a verify-stage result
- **AND** it MUST verify that read-only exFAT boot assets remain readable

#### Scenario: unavailable emulator or toolchain is recorded
- **WHEN** QEMU, Bochs, the cross toolchain, ROM/display support, or the required disk image path is unavailable
- **THEN** validation MUST be recorded as skipped or blocked with the missing dependency and residual risk
- **AND** it MUST NOT be reported as runtime-passed

### Requirement: 持久后端复用目录树 clean-sync 语义
BigOS SHALL make the persistent clean-sync `/rw` backend reuse the bounded writable directory tree semantics for nested directory creation, empty directory removal, multiple regular-file creation/removal within directories, metadata visibility, bounded enumeration, and deterministic failure behavior. The persistence guarantee MUST remain limited to successful `fsync`, explicit sync, cache write-back, or validation clean shutdown boundaries. This capability MUST NOT claim journaling, crash recovery, power-loss safety, automatic migration, broad storage/device support, or persistence for unsynchronized dirty state.

#### Scenario: 同步后的目录树跨 clean reboot 可见
- **WHEN** a process creates a nested directory, creates multiple files inside it, removes selected entries, synchronizes the persistent `/rw` state successfully, and validation starts a second emulator run with the same persistent test disk
- **THEN** BigOS MUST remount the persistent `/rw` volume
- **AND** the synchronized directory tree state MUST be visible through lookup, metadata queries, bounded enumeration, and fd I/O

#### Scenario: 未同步目录树变更不扩大承诺
- **WHEN** a directory tree mutation succeeds in cache but is not successfully synchronized before reboot
- **THEN** BigOS MUST NOT claim that the mutation persists across reboot
- **AND** previously synchronized filesystem state MUST remain explainable within the non-journaled clean-sync boundary

#### Scenario: 双阶段 marker 验证目录树持久性
- **WHEN** persistent writable smoke validation runs with the existing two-stage marker flow and the required persistent test disk is available
- **THEN** the first stage MUST create and mutate a bounded directory tree, synchronize it, and emit the existing write-stage success or failure marker family
- **AND** the second stage MUST reboot with the same persistent test disk, verify the synchronized directory tree through lookup, metadata, enumeration, and fd I/O, and emit the existing verify-stage success or failure marker family
- **AND** validation MUST NOT use this flow to claim crash recovery or persistence for unsynchronized dirty state

### Requirement: 持久目录树失败不破坏已同步状态
BigOS SHALL keep persistent directory tree failure behavior deterministic and state-preserving. Failed nested create, empty directory removal, unlink, enumeration, metadata update, sync, or cache write-back MUST NOT publish partially initialized files, directories, removed parent entries, leaked inode metadata, dirty-cache success, or corrupted read-only exFAT state.

#### Scenario: 持久目录创建失败不发布半成品
- **WHEN** persistent `/rw` directory creation requires a directory slot, inode, data block, cache block, or kernel allocation that is unavailable
- **THEN** BigOS MUST return a deterministic capacity, memory, or I/O error
- **AND** after the failure, later lookup, metadata query, and directory enumeration MUST observe the pre-failure persistent state

#### Scenario: 同步失败不报告目录树持久成功
- **WHEN** `fsync`, explicit sync, or cache eviction fails while writing dirty directory tree metadata to the persistent block device
- **THEN** BigOS MUST report a deterministic write-back error
- **AND** it MUST NOT mark the affected metadata clean or report the attempted directory tree update as durably committed

### Requirement: 持久后端同步文件增长跨 clean reboot 可见
BigOS SHALL make successfully synchronized persistent `/rw` file growth visible after a clean reboot using the same persistent test disk. The clean-sync guarantee MUST include synchronized append writes, seek-past-EOF writes with zero-read gaps, cross-block writes, file size metadata, and the block mappings needed to read the synchronized content. This requirement MUST NOT claim journal replay, crash recovery, power-loss safety, or persistence for unsynchronized dirty state.

#### Scenario: 同步后的扩展文件第二次启动读回
- **WHEN** a user process grows a persistent `/rw` regular file, calls `fsync` or equivalent explicit sync successfully, and validation starts a second emulator run with the same persistent test disk
- **THEN** BigOS MUST remount the persistent `/rw` volume
- **AND** reopening the file MUST return the synchronized content, zero-filled gap ranges, and bounded size metadata

#### Scenario: 未同步增长不扩大持久性承诺
- **WHEN** a persistent `/rw` file grows in cache but the affected data and metadata are not successfully synchronized before reboot
- **THEN** BigOS MUST NOT claim that the growth survives reboot
- **AND** previously synchronized filesystem state MUST remain explainable within the non-journaled clean-sync boundary

### Requirement: 持久后端同步截断跨 clean reboot 可见
BigOS SHALL make successfully synchronized persistent `/rw` truncate results visible after a clean reboot. The guarantee MUST include the truncated file size, retained prefix contents, zero-filled extended ranges, released-block ownership metadata, and absence of user-visible stale data from reused blocks. Failed or unsynchronized truncates MUST NOT be described as durable.

#### Scenario: 同步后的收缩截断第二次启动可见
- **WHEN** a persistent `/rw` file is truncated to a smaller size and the change synchronizes successfully before a clean reboot
- **THEN** the second run MUST observe the smaller size and retained prefix contents
- **AND** reads beyond the new EOF MUST follow the existing EOF behavior

#### Scenario: 同步后的扩展截断第二次启动零读
- **WHEN** a persistent `/rw` file is truncated to a larger size and the change synchronizes successfully before a clean reboot
- **THEN** the second run MUST observe the larger size
- **AND** the newly extended range MUST read as zero bytes until overwritten

### Requirement: 持久块分配失败保持已同步状态
BigOS SHALL keep persistent `/rw` state explainable when file growth, truncate, free-space metadata update, cache write-back, or backing block I/O fails. Failed persistent operations MUST NOT publish partially initialized blocks, leaked free-space metadata, duplicated block ownership, truncated metadata, dirty-cache success, or corrupted read-only exFAT state.

#### Scenario: 持久块耗尽不发布半成品
- **WHEN** persistent `/rw` file growth or truncate requires a data block, inode mapping, cache block, or kernel allocation that is unavailable
- **THEN** BigOS MUST return a deterministic capacity or memory error
- **AND** later lookup, read, metadata query, and directory enumeration MUST observe the pre-failure persistent state

#### Scenario: 持久写回失败不清除 pending state
- **WHEN** `fsync`, explicit sync, or cache eviction fails while writing persistent file growth or truncate metadata
- **THEN** BigOS MUST report a deterministic write-back error
- **AND** it MUST NOT mark the affected filesystem state as durably committed

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

### Requirement: 持久同步经缓存回写后才报告成功
BigOS SHALL make persistent `/rw` clean-sync success depend on successful page/buffer cache write-back of the required dirty file data and filesystem metadata blocks. A successful `fsync`, explicit synchronization, or clean validation boundary MUST NOT report durable success until the required dirty cache blocks for the persistent backing device have been written successfully. This requirement MUST NOT claim crash recovery, journal replay, power-loss safety, async I/O, or persistence for unsynchronized dirty state.

#### Scenario: fsync 成功扩大 clean-sync 承诺
- **WHEN** a process writes a bounded persistent `/rw` file, updates necessary metadata, and calls `fsync` from a blockable process context
- **THEN** BigOS MUST write the required dirty data and metadata cache blocks to the persistent backing device before `fsync` returns success
- **AND** after a clean reboot and remount with the same persistent test disk, reopening the file MUST return the synchronized content and metadata

#### Scenario: fsync 回写失败不声明持久成功
- **WHEN** persistent `/rw` `fsync` reaches a cache or backing-device write-back failure
- **THEN** BigOS MUST return a deterministic error
- **AND** it MUST NOT claim that the attempted update survives a clean reboot

#### Scenario: 未同步 dirty 状态不扩大承诺
- **WHEN** persistent `/rw` contains dirty cache blocks that have not been successfully synchronized
- **THEN** BigOS MUST NOT describe those blocks as durable
- **AND** previously synchronized filesystem state MUST remain explainable within the bounded clean-sync contract

### Requirement: 持久缓存淘汰结果跨重载可观察
BigOS SHALL make successful dirty cache eviction for persistent `/rw` observable through later reloads from the persistent backing device. When eviction write-back succeeds, the cache MAY reuse the slot and later reads MUST reload the written content or metadata from the device. When eviction write-back fails, BigOS MUST keep the dirty or pending state and MUST NOT reuse the slot as if the state were durable.

#### Scenario: 淘汰写回成功后重载读回
- **WHEN** a persistent `/rw` dirty block is unreferenced, selected for eviction, and successfully written back
- **THEN** the cache MAY reuse the slot
- **AND** a later read that reloads the evicted block from the persistent backing device MUST observe the synchronized content

#### Scenario: 淘汰写回失败不发布 durable state
- **WHEN** persistent `/rw` dirty block eviction fails during backing-device write-back
- **THEN** BigOS MUST preserve dirty or pending state for that block
- **AND** it MUST NOT report the affected filesystem state as durably synchronized

### Requirement: 持久同步验证记录环境边界
BigOS SHALL provide default-off validation for persistent `/rw` cache-backed synchronization under the current x86_64 Legacy BIOS emulator path. Validation MUST cover successful `fsync`, clean reboot readback, eviction write-back readback, and deterministic failure or skipped/blocked reporting where backing toolchain, emulator, ROM/display, serial capture, or persistent disk dependencies are unavailable.

#### Scenario: 双启动验证缓存回写后的状态
- **WHEN** persistent synchronization validation runs with the required toolchain, emulator, serial capture, and persistent test disk available
- **THEN** the first run MUST write bounded files and metadata under persistent `/rw`, synchronize them through the cache write-back path, and stop at a clean validation boundary
- **AND** the second run MUST remount the same persistent test disk and verify the synchronized data and metadata

#### Scenario: 环境不可用时不声称通过
- **WHEN** xmake, the x86_64 cross toolchain, QEMU or Bochs, ROM/display support, serial capture, or the persistent test disk is unavailable
- **THEN** validation MUST record the missing dependency and residual risk as skipped or blocked
- **AND** it MUST NOT report runtime persistent synchronization validation as passed

### Requirement: 持久 clean-sync 可显式使用现代存储后端
BigOS SHALL allow persistent clean-sync `/rw` validation or an equivalent explicit internal configuration to use a selected modern block-storage backend as its backing block device. This MUST remain opt-in and MUST NOT replace the default boot, default exFAT, default ATA, or default `/rw` backend policy.

#### Scenario: 显式现代后端挂载持久卷
- **WHEN** persistent `/rw` validation explicitly selects a ready modern block-storage backend containing a compatible persistent volume
- **THEN** BigOS MUST mount or validate that volume through the existing persistent clean-sync filesystem path
- **AND** all backing I/O MUST flow through the block request layer and page/buffer cache rather than modern-driver private filesystem hooks

#### Scenario: 默认路径不自动改用现代后端
- **WHEN** normal boot runs without the explicit modern-backend persistent validation or configuration
- **THEN** BigOS MUST keep the existing default boot and `/rw` backend behavior
- **AND** it MUST NOT silently mount a modern-backend volume as `/rw` merely because the device is present

### Requirement: 现代后端持久同步经缓存写回后才成功
Persistent `/rw` clean-sync success on a modern block-storage backend SHALL depend on successful page/buffer cache writeback of the required dirty file data and filesystem metadata blocks to that backend. `fsync`, explicit sync, eviction writeback, or a clean validation boundary MUST NOT report durable success until the selected writes reach terminal success through the request layer.

#### Scenario: fsync 成功后现代后端 clean reboot 读回
- **WHEN** a process writes bounded data or metadata under persistent `/rw` backed by the explicitly selected modern backend and `fsync` returns success
- **THEN** BigOS MUST have written the required dirty cache blocks to the modern backend through the request layer before returning success
- **AND** a later clean validation reboot using the same modern-backend disk image MUST observe the synchronized content and metadata

#### Scenario: 现代后端写回失败不声明持久成功
- **WHEN** persistent `/rw` synchronization on the modern backend encounters request rejection, queue exhaustion, issue failure, timeout, completion rejection, or device error
- **THEN** BigOS MUST return a deterministic synchronization or writeback error
- **AND** it MUST NOT mark the affected persistent state as durably committed or clear the required dirty state as though it were written

### Requirement: 现代后端持久验证保持 clean-sync 边界
BigOS SHALL provide default-off validation for persistent `/rw` on the explicitly selected modern backend that records clean-sync behavior and environment availability. The validation MUST NOT claim crash consistency, journal replay, power-loss safety, default storage replacement, or persistence for unsynchronized dirty state.

#### Scenario: 双阶段验证现代后端持久状态
- **WHEN** the required toolchain, emulator, serial capture, modern storage device configuration, and persistent disk image are available
- **THEN** validation MUST run a clean-sync flow that writes bounded `/rw` data or metadata, synchronizes it, and verifies it after a clean reboot or equivalent remount using the same modern-backend image
- **AND** it MUST report success only for state synchronized through cache writeback and request-layer terminal success

#### Scenario: 未同步 dirty 状态不扩大承诺
- **WHEN** persistent `/rw` backed by the modern backend contains dirty cache blocks that have not synchronized successfully before the clean validation boundary
- **THEN** BigOS MUST NOT describe those blocks as durable
- **AND** previously synchronized persistent state MUST remain explainable within the bounded clean-sync contract

#### Scenario: 环境不可用时记录跳过
- **WHEN** the modern-backend persistent validation cannot run because the cross toolchain, emulator, serial capture, device configuration, MSI-X delivery, disk image, or volume metadata is unavailable
- **THEN** validation notes MUST record the missing dependency and residual risk as skipped or blocked
- **AND** they MUST NOT report runtime persistent validation as passed

### Requirement: persistent /rw clean-sync advances to journal-first commit
BigOS SHALL advance persistent `/rw` clean-sync commits to a journal-first write path when the mounted volume uses the journal-capable bigfs format. Successful `fsync`, explicit sync, cache eviction of protected persistent blocks, and high-level metadata commit paths MUST honor the write-ahead journal ordering before reporting durable success. This requirement MUST NOT claim mount-time journal replay, crash recovery, power-loss safety, automatic repair, or persistence for unsynchronized dirty state.

#### Scenario: fsync uses journal-first durable path
- **WHEN** a process modifies persistent `/rw` file data and metadata and calls `fsync` from a blockable process context
- **THEN** BigOS MUST synchronize the required journal transaction before synchronizing the corresponding home-location data and metadata blocks
- **AND** `fsync` MUST return success only after the journaled transaction and home-location updates complete according to the ordered commit plan

#### Scenario: journaled commit failure does not report clean-sync success
- **WHEN** journal record write-back, journal commit marker write-back, home-location write-back, or journal checkpoint/clear write-back fails
- **THEN** BigOS MUST return a deterministic write-back error
- **AND** it MUST NOT describe the attempted filesystem state as durably committed or clean-sync eligible

### Requirement: journal-capable persistent format is explicit
BigOS SHALL require explicit formatting or a compatible journal-capable persistent bigfs format before enabling the journaled persistent `/rw` path. The journal-capable format MUST reserve exactly 32 filesystem blocks for the write-ahead journal. Mount validation MUST reject incompatible format versions, invalid journal bounds, invalid journal sequence metadata, or contradictions between the journal metadata and filesystem metadata.

#### Scenario: old clean-sync image is not silently upgraded
- **WHEN** normal boot attempts to mount a persistent bigfs image created before the journal-capable format
- **THEN** BigOS MUST reject persistent `/rw` publication or fall back according to the documented policy
- **AND** it MUST NOT silently rewrite the superblock, auto-migrate metadata, or publish the old image as journal-protected storage

#### Scenario: explicit format publishes journal-capable /rw
- **WHEN** the explicit persistent format path succeeds on a supported persistent test disk
- **THEN** BigOS MUST publish persistent `/rw` using the journal-capable format
- **AND** later synchronized filesystem mutations MUST use the journal-first commit path

### Requirement: persistent /rw reports unfinished journal state conservatively
BigOS SHALL treat unfinished journal state conservatively until mount-time recovery is implemented. If mount validation detects a journal state that requires replay or discard, the persistent volume MUST NOT be published as clean writable storage. Ordinary boot MAY fall back to RAM-backed `/rw` only when it records an explicit diagnostic that persistent `/rw` was withheld because journal recovery is required.

#### Scenario: mount sees journal requiring replay
- **WHEN** persistent mount validation sees a committed transaction that has not been checkpointed into home-location blocks
- **THEN** BigOS MUST reject publication of that persistent volume as writable `/rw`
- **AND** it MUST NOT expose paths, metadata, or file contents from that volume as recovered state
- **AND** ordinary boot fallback to RAM-backed `/rw`, if used, MUST be diagnostically distinguishable from a successful persistent mount

#### Scenario: mount sees partial uncommitted journal
- **WHEN** persistent mount validation sees partial journal records without a durable commit marker
- **THEN** BigOS MUST treat the volume according to the documented pre-recovery policy
- **AND** it MUST NOT claim that discard or repair has been completed unless a later recovery capability implements it
